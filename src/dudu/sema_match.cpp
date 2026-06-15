#include "dudu/sema_match.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_enum.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <sstream>
#include <string>

namespace dudu {

namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

bool has_expr(const Expr& expr) {
    return !expr.text.empty();
}

const SourceLocation& node_location(const SourceLocation& fallback, const Expr& expr) {
    return expr.location.line == 0 ? fallback : expr.location;
}

std::string join_strings(const std::vector<std::string>& values, std::string_view separator) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << values[i];
    }
    return out.str();
}

std::optional<std::string> enum_case_variant(const EnumDecl& en, const Stmt& stmt) {
    if (stmt.pattern_expr.kind == ExprKind::Name && stmt.pattern_expr.name == "_") {
        return std::string{"_"};
    }
    const Expr* pattern = &stmt.pattern_expr;
    if (stmt.pattern_expr.kind == ExprKind::Call && !stmt.pattern_expr.callee.empty()) {
        pattern = &stmt.pattern_expr.callee.front();
    }
    const std::optional<std::string> path = member_path_from_expr(*pattern);
    if (!path) {
        return std::nullopt;
    }
    const std::string prefix = en.name + ".";
    if (!starts_with(*path, prefix)) {
        return std::nullopt;
    }
    const std::string variant = path->substr(prefix.size());
    if (variant.find('.') != std::string::npos) {
        return std::nullopt;
    }
    return variant;
}

bool bind_payload_case(FunctionScope& nested, const EnumValueDecl& value, const Expr& pattern,
                       const SourceLocation& location) {
    if (pattern.kind != ExprKind::Call) {
        if (!value.payload_fields.empty()) {
            fail(location, "payload case requires destructuring: " + value.name);
        }
        return false;
    }
    if (pattern.children.size() != value.payload_fields.size()) {
        fail(location, "case " + value.name + " expects " +
                           std::to_string(value.payload_fields.size()) + " bindings, got " +
                           std::to_string(pattern.children.size()));
    }
    std::set<std::string> seen_named_fields;
    for (size_t i = 0; i < pattern.children.size(); ++i) {
        const Expr& binding = pattern.children[i];
        const EnumPayloadField* field = &value.payload_fields[i];
        const Expr* binding_name = &binding;
        if (binding.kind == ExprKind::NamedArg && binding.children.size() == 1) {
            if (!seen_named_fields.insert(binding.name).second) {
                fail(binding.location,
                     "duplicate enum payload field in pattern: " + value.name + "." + binding.name);
            }
            const auto found =
                std::find_if(value.payload_fields.begin(), value.payload_fields.end(),
                             [&](const EnumPayloadField& payload_field) {
                                 return payload_field.name == binding.name;
                             });
            if (found == value.payload_fields.end()) {
                fail(binding.location,
                     "unknown enum payload field in pattern: " + value.name + "." + binding.name);
            }
            field = &*found;
            binding_name = &binding.children.front();
        }
        if (binding_name->kind != ExprKind::Name || binding_name->name.empty()) {
            fail(binding.location, "payload case bindings must be names");
        }
        nested.locals[binding_name->name] = field->type;
    }
    return true;
}

enum class WrapperMatchKind {
    None,
    Option,
    Result,
};

struct WrapperMatchType {
    WrapperMatchKind kind = WrapperMatchKind::None;
    std::vector<std::string> args;
};

WrapperMatchType wrapper_match_type(const std::string& type) {
    const std::string trimmed = trim(type);
    if (starts_with(trimmed, "Option[") && trimmed.back() == ']') {
        return {.kind = WrapperMatchKind::Option,
                .args = split_top_level_args(trimmed.substr(7, trimmed.size() - 8))};
    }
    if (starts_with(trimmed, "Result[") && trimmed.back() == ']') {
        return {.kind = WrapperMatchKind::Result,
                .args = split_top_level_args(trimmed.substr(7, trimmed.size() - 8))};
    }
    return {};
}

std::optional<std::string> wrapper_case_name(const Expr& pattern) {
    if (pattern.kind == ExprKind::Name && pattern.name == "_") {
        return std::string{"_"};
    }
    if (pattern.kind == ExprKind::NoneLiteral) {
        return std::string{"None"};
    }
    if (pattern.kind == ExprKind::Call && !pattern.callee.empty() &&
        pattern.callee.front().kind == ExprKind::Name) {
        return pattern.callee.front().name;
    }
    return std::nullopt;
}

void bind_wrapper_case(FunctionScope& nested, const WrapperMatchType& wrapper, const Expr& pattern,
                       const SourceLocation& location) {
    const std::optional<std::string> name = wrapper_case_name(pattern);
    if (!name || *name == "_" || *name == "None") {
        return;
    }
    if (pattern.kind != ExprKind::Call || pattern.children.size() != 1 ||
        pattern.children.front().kind != ExprKind::Name || pattern.children.front().name.empty()) {
        fail(location, "wrapper payload case expects one binding name");
    }
    if (wrapper.kind == WrapperMatchKind::Option && *name == "Some" && wrapper.args.size() == 1) {
        nested.locals[pattern.children.front().name] = trim(wrapper.args[0]);
        return;
    }
    if (wrapper.kind == WrapperMatchKind::Result && wrapper.args.size() == 2) {
        if (*name == "Ok") {
            nested.locals[pattern.children.front().name] = trim(wrapper.args[0]);
            return;
        }
        if (*name == "Err") {
            nested.locals[pattern.children.front().name] = trim(wrapper.args[1]);
            return;
        }
    }
}

bool check_wrapper_match(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                         int loop_depth, const WrapperMatchType& wrapper,
                         const MatchCheckCallbacks& callbacks) {
    const std::set<std::string> expected = wrapper.kind == WrapperMatchKind::Option
                                               ? std::set<std::string>{"Some", "None"}
                                               : std::set<std::string>{"Ok", "Err"};
    std::set<std::string> covered;
    bool wildcard = false;
    for (const Stmt& child : stmt.children) {
        if (child.kind != StmtKind::Case) {
            fail(child.location, "match body expects case statements");
        }
        if (wildcard) {
            fail(child.location, "unreachable case after wildcard");
        }
        const std::optional<std::string> name = wrapper_case_name(child.pattern_expr);
        if (!name || (!expected.contains(*name) && *name != "_")) {
            fail(child.location, wrapper.kind == WrapperMatchKind::Option
                                     ? "case pattern must be Some(...), None, or _"
                                     : "case pattern must be Ok(...), Err(...), or _");
        }
        if (*name == "_") {
            if (!has_expr(child.guard_expr)) {
                wildcard = true;
            }
        } else if (!has_expr(child.guard_expr) && !covered.insert(*name).second) {
            fail(child.location, "unreachable duplicate case: " + *name);
        }
        FunctionScope nested = scope;
        bind_wrapper_case(nested, wrapper, child.pattern_expr, child.location);
        if (has_expr(child.guard_expr)) {
            const std::string guard_type = callbacks.infer_expr(
                nested, child.guard_expr, &node_location(child.location, child.guard_expr));
            if (guard_type != "bool") {
                fail(node_location(child.location, child.guard_expr),
                     "match guard must be bool, got " + guard_type);
            }
        }
        callbacks.check_block(nested, child.children, return_type, loop_depth);
    }
    if (!wildcard && covered != expected) {
        std::vector<std::string> missing;
        for (const std::string& name : expected) {
            if (!covered.contains(name)) {
                missing.push_back(name);
            }
        }
        fail(stmt.location,
             "non-exhaustive match on wrapper; missing cases: " + join_strings(missing, ", "));
    }
    return true;
}

bool enum_contains_variant(const EnumDecl& en, const std::string& variant) {
    for (const EnumValueDecl& value : en.values) {
        if (value.name == variant) {
            return true;
        }
    }
    return false;
}

std::string missing_enum_cases(const EnumDecl& en, const std::set<std::string>& covered) {
    std::ostringstream out;
    bool first = true;
    for (const EnumValueDecl& value : en.values) {
        if (covered.contains(value.name)) {
            continue;
        }
        if (!first) {
            out << ", ";
        }
        first = false;
        out << en.name << "." << value.name;
    }
    return out.str();
}

void check_enum_match(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                      int loop_depth, const MatchCheckCallbacks& callbacks, const EnumDecl& en) {
    std::set<std::string> covered;
    bool wildcard = false;
    for (const Stmt& child : stmt.children) {
        if (child.kind != StmtKind::Case) {
            fail(child.location, "match body expects case statements");
        }
        if (wildcard) {
            fail(child.location, "unreachable case after wildcard");
        }
        const std::optional<std::string> variant = enum_case_variant(en, child);
        if (!variant) {
            fail(child.location, "case pattern must be " + en.name + ".Variant or _");
        }
        if (*variant == "_") {
            if (!has_expr(child.guard_expr)) {
                wildcard = true;
            }
        } else {
            if (!enum_contains_variant(en, *variant)) {
                fail(child.location, "unknown enum variant in pattern: " + en.name + "." + *variant);
            }
            if (!has_expr(child.guard_expr) && !covered.insert(*variant).second) {
                fail(child.location, "unreachable duplicate case: " + en.name + "." + *variant);
            }
        }
        FunctionScope nested = scope;
        if (*variant != "_") {
            const EnumValueDecl* value = enum_variant_decl(en, *variant);
            if (value != nullptr) {
                bind_payload_case(nested, *value, child.pattern_expr, child.location);
            }
        }
        if (has_expr(child.guard_expr)) {
            const std::string guard_type = callbacks.infer_expr(
                nested, child.guard_expr, &node_location(child.location, child.guard_expr));
            if (guard_type != "bool") {
                fail(node_location(child.location, child.guard_expr),
                     "match guard must be bool, got " + guard_type);
            }
        }
        callbacks.check_block(nested, child.children, return_type, loop_depth);
    }
    if (!wildcard && covered.size() != en.values.size()) {
        fail(stmt.location, "non-exhaustive match on " + en.name +
                                "; missing cases: " + missing_enum_cases(en, covered));
    }
}

} // namespace

void check_match_stmt(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                      int loop_depth, const MatchCheckCallbacks& callbacks) {
    const SourceLocation& subject_location = node_location(stmt.location, stmt.condition_expr);
    const std::string subject_type =
        callbacks.infer_expr(scope, stmt.condition_expr, &subject_location);
    const WrapperMatchType wrapper = wrapper_match_type(subject_type);
    if (wrapper.kind != WrapperMatchKind::None) {
        check_wrapper_match(scope, stmt, return_type, loop_depth, wrapper, callbacks);
        return;
    }
    const EnumDecl* en = enum_decl_for_type(scope.symbols, subject_type);
    if (en == nullptr) {
        fail(subject_location, "match subject must be an enum, got " + subject_type);
    }
    check_enum_match(scope, stmt, return_type, loop_depth, callbacks, *en);
}

} // namespace dudu
