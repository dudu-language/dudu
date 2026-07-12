#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_scope.hpp"
#include "dudu/sema/type_compat.hpp"

#include <sstream>

namespace dudu {
namespace {

std::optional<std::string> expr_path_key(const Expr& expr) {
    const std::optional<ExprPath> path = expr_path_from_expr(expr);
    return path ? std::optional<std::string>{render_expr_path(*path)} : std::nullopt;
}

bool is_native_enum_value_expr(const FunctionScope& scope, const Expr& expr,
                               const TypeRef& expected) {
    if (!type_ref_is_integer(resolve_alias_ref(scope.symbols, expected))) {
        return false;
    }
    const std::optional<std::string> path = expr_path_key(expr);
    return path && scope.symbols.native_enum_values.contains(*path);
}

std::optional<TypeRef> index_marker_type_ref(const Expr& expr) {
    if (expr.kind == ExprKind::Ellipsis) {
        return named_type_ref("ellipsis", expr.location);
    }
    if (expr.kind == ExprKind::NewAxis) {
        return named_type_ref("new_axis", expr.location);
    }
    return std::nullopt;
}

bool pack_expansion_arg_matches(const FunctionScope& scope, const FunctionSignature& signature,
                                size_t arg_index, size_t arg_count, const Expr& arg,
                                const TypeRef& expected, const TypeRef& got) {
    if (!signature.variadic || arg.kind != ExprKind::PackExpansion || arg.children.size() != 1 ||
        got.kind != TypeKind::PackExpansion || got.children.size() != 1) {
        return false;
    }
    const size_t param_index = signature_param_index_for_arg(signature, arg_index, arg_count);
    if (param_index != signature_variadic_param_index(signature)) {
        return false;
    }
    return can_assign_ast(scope, expected, arg.children.front(), got.children.front());
}

bool call_arg_matches_ast(const FunctionScope& scope, const FunctionSignature& signature,
                          size_t arg_index, const std::vector<Expr>& args, const TypeRef& expected,
                          const TypeRef& got) {
    return can_assign_ast(scope, expected, args[arg_index], got) ||
           pack_expansion_arg_matches(scope, signature, arg_index, args.size(), args[arg_index],
                                      expected, got);
}

std::string template_args_label(const Expr& expr) {
    std::ostringstream out;
    const std::vector<TypeRef> args = template_type_refs(expr);
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << substitute_type_ref_text(args[i], {});
    }
    return out.str();
}

} // namespace
bool can_assign_ast(const FunctionScope& scope, const TypeRef& expected, const Expr& expr,
                    const TypeRef& got) {
    const TypeRef resolved_expected = resolve_alias_ref(scope.symbols, expected);
    const TypeRef resolved_got = resolve_alias_ref(scope.symbols, got);
    if (is_native_enum_value_expr(scope, expr, resolved_expected)) {
        return true;
    }
    return assignment_type_allowed(scope.symbols, expected, expr, got) ||
           assignment_type_allowed(scope.symbols, resolved_expected, expr, resolved_got) ||
           native_base_assignable(scope.symbols, expected, got);
}

bool is_builtin_call(const std::string& callee) {
    static const std::set<std::string> builtins = {"delete", "free", "len",   "max",
                                                   "min",    "move", "print", "range"};
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
    const std::string type_display = substitute_type_ref_text(type, {});
    std::ostringstream out;
    out << "cannot construct abstract class: " << type_display << "; missing ";
    for (size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << missing[i];
    }
    sema_expr_fail(*location, out.str());
}

bool is_comparison_op(std::string_view op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

bool parse_local_function_type(const FunctionScope& scope, const std::string& name,
                               FunctionSignature& out) {
    const TypeRef local_type = local_type_ref(scope, name);
    if (has_type_ref(local_type) && parse_function_type_or_alias(scope.symbols, local_type, out)) {
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
    const size_t min_arg_count = signature_min_arg_count(signature);
    if ((!signature.variadic && args.size() != param_count) ||
        (signature.variadic && args.size() < min_arg_count)) {
        sema_expr_fail(*location, "function " + callee + " expects " + std::to_string(param_count) +
                                      " arguments, got " + std::to_string(args.size()));
    }
    for (size_t i = 0; i < args.size(); ++i) {
        const TypeRef expected = signature_param_type_ref(
            signature, signature_param_index_for_arg(signature, i, args.size()));
        const std::optional<TypeRef> marker_type = index_marker_type_ref(args[i]);
        const TypeRef got_ref =
            marker_type ? *marker_type : infer_expr_type_ast(scope, args[i], location);
        if (!call_arg_matches_ast(scope, signature, i, args, expected, got_ref)) {
            const std::string expected_display = substitute_type_ref_text(expected, {});
            const std::string got_display = substitute_type_ref_text(got_ref, {});
            sema_expr_fail(*location, "argument " + std::to_string(i + 1) + " for " + callee +
                                          " expects " + expected_display + ", got " + got_display);
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
        const TypeRef expected = field->type_ref;
        if (!can_assign_ast(scope, expected, *arg, got_ref)) {
            const std::string got_display = substitute_type_ref_text(got_ref, {});
            const std::string expected_display = type_ref_text(expected);
            sema_expr_fail(arg->location, "argument " + std::to_string(i + 1) + " for " + en.name +
                                              "." + value.name + " expects " + expected_display +
                                              ", got " + got_display);
        }
    }
}

std::vector<TypeRef> infer_call_arg_types(const FunctionScope& scope,
                                          const std::vector<Expr>& args) {
    std::vector<TypeRef> types;
    types.reserve(args.size());
    for (const Expr& arg : args) {
        const std::optional<TypeRef> marker_type = index_marker_type_ref(arg);
        types.push_back(marker_type ? *marker_type : infer_expr_type_ast(scope, arg, nullptr));
    }
    return types;
}

bool call_args_match_with_types(const FunctionScope& scope, const FunctionSignature& signature,
                                const std::vector<Expr>& args,
                                const std::vector<TypeRef>& arg_types) {
    const size_t param_count = signature_param_count(signature);
    const size_t min_arg_count = signature_min_arg_count(signature);
    if ((!signature.variadic && args.size() != param_count) ||
        (signature.variadic && args.size() < min_arg_count)) {
        return false;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        const TypeRef expected = signature_param_type_ref(
            signature, signature_param_index_for_arg(signature, i, args.size()));
        if (!call_arg_matches_ast(scope, signature, i, args, expected, arg_types[i])) {
            return false;
        }
    }
    return true;
}

bool call_args_match_ast(const FunctionScope& scope, const FunctionSignature& signature,
                         const std::vector<Expr>& args) {
    return call_args_match_with_types(scope, signature, args, infer_call_arg_types(scope, args));
}

std::optional<FunctionSignature>
matching_signature_ast(const FunctionScope& scope, const std::vector<FunctionSignature>& options,
                       const std::vector<Expr>& args) {
    const std::optional<size_t> index = matching_signature_index_ast(scope, options, args);
    return index ? std::optional<FunctionSignature>{options[*index]} : std::nullopt;
}

std::optional<size_t> matching_signature_index_ast(const FunctionScope& scope,
                                                   const std::vector<FunctionSignature>& options,
                                                   const std::vector<Expr>& args) {
    if (options.empty()) {
        return std::nullopt;
    }
    const std::vector<TypeRef> arg_types = infer_call_arg_types(scope, args);
    std::optional<size_t> selected;
    int selected_score = 0;
    for (size_t option_index = 0; option_index < options.size(); ++option_index) {
        const FunctionSignature& signature = options[option_index];
        if (!call_args_match_with_types(scope, signature, args, arg_types)) {
            continue;
        }
        int score = 0;
        for (size_t arg_index = 0; arg_index < args.size(); ++arg_index) {
            const size_t param_index =
                signature_param_index_for_arg(signature, arg_index, args.size());
            const TypeRef expected =
                resolve_alias_ref(scope.symbols, signature_param_type_ref(signature, param_index));
            const TypeRef got = resolve_alias_ref(scope.symbols, arg_types[arg_index]);
            if (!type_ref_equivalent(expected, got)) {
                ++score;
            }
        }
        if (!selected || score < selected_score) {
            selected = option_index;
            selected_score = score;
        }
    }
    return selected;
}

std::string template_call_callee(const FunctionScope& scope, const Expr& expr,
                                 const SourceLocation* location) {
    std::ostringstream out;
    if (const std::optional<ExprPath> path = scoped_call_callee_path(scope, expr, location)) {
        out << render_expr_path(*path);
    } else {
        return {};
    }
    out << "[" << template_args_label(expr) << "]";
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
