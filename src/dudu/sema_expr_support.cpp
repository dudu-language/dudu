#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_scope.hpp"

namespace dudu {
namespace {

std::optional<std::string> expr_path_key(const Expr& expr) {
    const std::optional<ExprPath> path = expr_path_from_expr(expr);
    return path ? std::optional<std::string>{render_expr_path(*path)} : std::nullopt;
}

bool is_native_enum_value_expr(const FunctionScope& scope, const Expr& expr,
                               const std::string& expected) {
    if (!is_integer_type(expected)) {
        return false;
    }
    const std::optional<std::string> path = expr_path_key(expr);
    return path && scope.symbols.native_enum_values.contains(*path);
}

} // namespace
bool can_assign_ast(const FunctionScope& scope, const TypeRef& expected, const Expr& expr,
                    const TypeRef& got) {
    const TypeRef resolved_expected = resolve_alias_ref(scope.symbols, expected);
    const TypeRef resolved_got = resolve_alias_ref(scope.symbols, got);
    const std::string expected_text = substitute_type_ref_text(resolved_expected, {});
    if (is_native_enum_value_expr(scope, expr, expected_text)) {
        return true;
    }
    return assignment_type_allowed(expected, expr, got) ||
           assignment_type_allowed(resolved_expected, expr, resolved_got) ||
           native_base_assignable(scope.symbols, expected, got);
}

bool is_builtin_call(const std::string& callee) {
    static const std::set<std::string> builtins = {"delete", "free",  "len",  "max",
                                                   "min",    "print", "range"};
    return builtins.contains(callee);
}
bool is_local_member_call(const FunctionScope& scope, const std::string& callee) {
    const size_t dot = callee.find('.');
    return dot != std::string::npos && scope.local_type_refs.contains(trim(callee.substr(0, dot)));
}
void reject_abstract_construction(const Symbols& symbols, const TypeRef& type,
                                  const SourceLocation* location) {
    if (location == nullptr) {
        return;
    }
    const std::vector<std::string> missing = unimplemented_abstract_methods(symbols, type);
    if (missing.empty()) {
        return;
    }
    const std::string type_text = substitute_type_ref_text(type, {});
    std::ostringstream out;
    out << "cannot construct abstract class: " << type_text << "; missing ";
    for (size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << missing[i];
    }
    sema_expr_fail(*location, out.str());
}

bool is_comparison_op(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

bool parse_local_function_type(const FunctionScope& scope, const std::string& name,
                               FunctionSignature& out) {
    std::set<std::string> seen_aliases;
    std::function<bool(const TypeRef&)> parse_ref = [&](const TypeRef& type_ref) -> bool {
        if (parse_function_type(type_ref, out)) {
            return true;
        }
        if (type_ref.kind != TypeKind::Named || !seen_aliases.insert(type_ref.name).second) {
            return false;
        }
        const auto alias = scope.symbols.alias_type_refs.find(type_ref.name);
        return alias != scope.symbols.alias_type_refs.end() && parse_ref(alias->second);
    };
    const TypeRef local_type = local_type_ref(scope, name);
    if (has_type_ref(local_type) && parse_ref(local_type)) {
        return true;
    }
    return false;
}

void check_call_args_ast(const FunctionScope& scope, const std::string& callee,
                         const FunctionSignature& signature, const std::vector<Expr>& args,
                         const SourceLocation* location) {
    if (location == nullptr)
        return;
    const size_t param_count = signature_param_count(signature);
    if ((!signature.variadic && args.size() != param_count) ||
        (signature.variadic && args.size() < param_count)) {
        sema_expr_fail(*location, "function " + callee + " expects " + std::to_string(param_count) +
                                      " arguments, got " + std::to_string(args.size()));
    }
    for (size_t i = 0; i < param_count; ++i) {
        const TypeRef expected = signature_param_type_ref(signature, i);
        const std::string expected_text = substitute_type_ref_text(expected, {});
        const TypeRef got_ref = infer_expr_type_ast(scope, args[i], location);
        const std::string got = substitute_type_ref_text(got_ref, {});
        if (!can_assign_ast(scope, expected, args[i], got_ref)) {
            sema_expr_fail(*location, "argument " + std::to_string(i + 1) + " for " + callee +
                                          " expects " + expected_text + ", got " + got);
        }
    }
}

void check_enum_variant_args_ast(const FunctionScope& scope, const EnumDecl& en,
                                 const EnumValueDecl& value, const std::vector<Expr>& args,
                                 const SourceLocation* location) {
    if (location == nullptr) {
        return;
    }
    if (args.size() != value.payload_fields.size()) {
        sema_expr_fail(*location, "enum constructor " + en.name + "." + value.name + " expects " +
                                      std::to_string(value.payload_fields.size()) +
                                      " arguments, got " + std::to_string(args.size()));
    }
    std::set<std::string> seen_named;
    for (size_t i = 0; i < args.size(); ++i) {
        const EnumPayloadField* field = &value.payload_fields[i];
        const Expr* arg = &args[i];
        if (args[i].kind == ExprKind::NamedArg) {
            const auto found = std::find_if(
                value.payload_fields.begin(), value.payload_fields.end(),
                [&](const EnumPayloadField& candidate) { return candidate.name == args[i].name; });
            if (found == value.payload_fields.end()) {
                sema_expr_fail(args[i].location, "unknown enum payload field: " + en.name + "." +
                                                     value.name + "." + args[i].name);
            }
            if (!seen_named.insert(args[i].name).second) {
                sema_expr_fail(args[i].location, "duplicate enum payload field: " + en.name + "." +
                                                     value.name + "." + args[i].name);
            }
            field = &*found;
            arg = &args[i].children.front();
        }
        const TypeRef got_ref = infer_expr_type_ast(scope, *arg, location);
        const std::string got = substitute_type_ref_text(got_ref, {});
        const TypeRef expected = field->type_ref;
        const std::string expected_text = type_ref_text(expected);
        if (!can_assign_ast(scope, expected, *arg, got_ref)) {
            sema_expr_fail(arg->location, "argument " + std::to_string(i + 1) + " for " + en.name +
                                              "." + value.name + " expects " + expected_text +
                                              ", got " + got);
        }
    }
}

bool call_args_match_ast(const FunctionScope& scope, const FunctionSignature& signature,
                         const std::vector<Expr>& args) {
    const size_t param_count = signature_param_count(signature);
    if ((!signature.variadic && args.size() != param_count) ||
        (signature.variadic && args.size() < param_count)) {
        return false;
    }
    for (size_t i = 0; i < param_count; ++i) {
        const TypeRef got = infer_expr_type_ast(scope, args[i], nullptr);
        if (!can_assign_ast(scope, signature_param_type_ref(signature, i), args[i], got)) {
            return false;
        }
    }
    return true;
}

std::optional<FunctionSignature>
matching_signature_ast(const FunctionScope& scope, const std::vector<FunctionSignature>& options,
                       const std::vector<Expr>& args) {
    for (const FunctionSignature& signature : options) {
        if (call_args_match_ast(scope, signature, args)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::string template_call_callee(const FunctionScope& scope, const Expr& expr,
                                 const SourceLocation* location) {
    std::ostringstream out;
    if (const std::optional<ExprPath> path = scoped_call_callee_path(scope, expr, location)) {
        out << render_expr_path(*path);
    } else {
        return {};
    }
    out << "[" << template_args_lookup_text(expr) << "]";
    return out.str();
}

bool is_offsetof_field_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Name || expr.kind == ExprKind::StringLiteral) {
        return true;
    }
    if (expr.kind == ExprKind::Member) {
        return expr_path_from_expr(expr).has_value();
    }
    return false;
}

} // namespace dudu
