#include "dudu/sema_match.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/match_patterns.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_enum.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <sstream>
#include <string>

namespace dudu {

namespace {

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

bool bind_payload_case(FunctionScope& nested, const EnumValueDecl& value, const Expr& pattern,
                       const SourceLocation& location) {
    if (pattern.kind != ExprKind::Call) {
        if (!value.payload_fields.empty()) {
            sema_fail(location, "payload case requires destructuring: " + value.name);
        }
        return false;
    }
    if (pattern.children.size() != value.payload_fields.size()) {
        sema_fail(location, "case " + value.name + " expects " +
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
                sema_fail(binding.location, "duplicate enum payload field in pattern: " +
                                                value.name + "." + binding.name);
            }
            const auto found =
                std::find_if(value.payload_fields.begin(), value.payload_fields.end(),
                             [&](const EnumPayloadField& payload_field) {
                                 return payload_field.name == binding.name;
                             });
            if (found == value.payload_fields.end()) {
                sema_fail(binding.location, "unknown enum payload field in pattern: " + value.name +
                                                "." + binding.name);
            }
            field = &*found;
            binding_name = &binding.children.front();
        }
        if (binding_name->kind != ExprKind::Name || binding_name->name.empty()) {
            sema_fail(binding.location, "payload case bindings must be names");
        }
        nested.locals[binding_name->name] = field->type;
    }
    return true;
}

void bind_wrapper_case(FunctionScope& nested, const WrapperMatchType& wrapper, const Expr& pattern,
                       const SourceLocation& location) {
    const std::optional<std::string> name = wrapper_case_name(pattern);
    if (!name || *name == "_" || *name == "None") {
        return;
    }
    if (pattern.kind != ExprKind::Call || pattern.children.size() != 1 ||
        pattern.children.front().kind != ExprKind::Name || pattern.children.front().name.empty()) {
        sema_fail(location, "wrapper payload case expects one binding name");
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
            sema_fail(child.location, "match body expects case statements");
        }
        if (wildcard) {
            sema_fail(child.location, "unreachable case after wildcard");
        }
        const std::optional<std::string> name = wrapper_case_name(child.pattern_expr);
        if (!name || (!expected.contains(*name) && *name != "_")) {
            sema_fail(child.location, wrapper.kind == WrapperMatchKind::Option
                                          ? "case pattern must be Some(...), None, or _"
                                          : "case pattern must be Ok(...), Err(...), or _");
        }
        if (*name == "_") {
            if (!sema_has_expr(child.guard_expr)) {
                wildcard = true;
            }
        } else if (!sema_has_expr(child.guard_expr) && !covered.insert(*name).second) {
            sema_fail(child.location, "unreachable duplicate case: " + *name);
        }
        FunctionScope nested = scope;
        bind_wrapper_case(nested, wrapper, child.pattern_expr, child.location);
        if (sema_has_expr(child.guard_expr)) {
            const std::string guard_type = callbacks.infer_expr(
                nested, child.guard_expr, &node_location(child.location, child.guard_expr));
            if (guard_type != "bool") {
                sema_fail(node_location(child.location, child.guard_expr),
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
        sema_fail(stmt.location,
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
            sema_fail(child.location, "match body expects case statements");
        }
        if (wildcard) {
            sema_fail(child.location, "unreachable case after wildcard");
        }
        const std::optional<std::string> variant = enum_case_variant_name_for(en, child);
        if (!variant) {
            sema_fail(child.location, "case pattern must be " + en.name + ".Variant or _");
        }
        if (*variant == "_") {
            if (!sema_has_expr(child.guard_expr)) {
                wildcard = true;
            }
        } else {
            if (!enum_contains_variant(en, *variant)) {
                sema_fail(child.location,
                          "unknown enum variant in pattern: " + en.name + "." + *variant);
            }
            if (!sema_has_expr(child.guard_expr) && !covered.insert(*variant).second) {
                sema_fail(child.location,
                          "unreachable duplicate case: " + en.name + "." + *variant);
            }
        }
        FunctionScope nested = scope;
        if (*variant != "_") {
            const EnumValueDecl* value = enum_variant_decl(en, *variant);
            if (value != nullptr) {
                bind_payload_case(nested, *value, child.pattern_expr, child.location);
            }
        }
        if (sema_has_expr(child.guard_expr)) {
            const std::string guard_type = callbacks.infer_expr(
                nested, child.guard_expr, &node_location(child.location, child.guard_expr));
            if (guard_type != "bool") {
                sema_fail(node_location(child.location, child.guard_expr),
                          "match guard must be bool, got " + guard_type);
            }
        }
        callbacks.check_block(nested, child.children, return_type, loop_depth);
    }
    if (!wildcard && covered.size() != en.values.size()) {
        sema_fail(stmt.location, "non-exhaustive match on " + en.name +
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
        sema_fail(subject_location, "match subject must be an enum, got " + subject_type);
    }
    check_enum_match(scope, stmt, return_type, loop_depth, callbacks, *en);
}

} // namespace dudu
