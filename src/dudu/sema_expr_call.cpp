#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
std::string infer_call_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* use_location) {
    const std::string callee = scoped_call_callee_text(scope, expr, use_location);
    if (callee.empty()) {
        return {};
    }
    if (const auto variant = enum_variant_from_path(scope.symbols, callee)) {
        check_enum_variant_args_ast(scope, *variant->first, *variant->second, expr.children,
                                    use_location);
        return variant->first->name;
    }
    if (is_super_call(callee)) {
        return infer_super_call_ast(
            scope, expr, callee, use_location,
            {.infer_expr_type =
                 [](const FunctionScope& nested, const Expr& arg, const SourceLocation* location) {
                     return infer_expr_type_ast(nested, arg, location);
                 },
             .can_assign =
                 [](const FunctionScope& nested, const std::string& expected, const Expr& value,
                    const std::string& got) {
                     return can_assign_ast(nested, expected, value, got);
                 },
             .matching_signature =
                 [](const FunctionScope& nested, const std::vector<FunctionSignature>& options,
                    const std::vector<Expr>& args) {
                     return matching_signature_ast(nested, options, args);
                 },
             .check_call_args =
                 [](const FunctionScope& nested, const std::string& nested_callee,
                    const FunctionSignature& signature, const std::vector<Expr>& args,
                    const SourceLocation* location) {
                     check_call_args_ast(nested, nested_callee, signature, args, location);
                 }});
    }
    if (const auto pointer_cast = infer_pointer_cast_call_ast(scope, expr, callee, use_location)) {
        return *pointer_cast;
    }
    if (callee == "Ok" || callee == "Err") {
        return substitute_type_ref_text(infer_expr_type_ast(scope, expr, use_location), {});
    }
    if (const auto generic_fn = scope.symbols.function_decls.find(callee);
        generic_fn != scope.symbols.function_decls.end() &&
        !generic_fn->second->generic_params.empty()) {
        if (const auto type_args = infer_generic_call_type_args(
                scope, *generic_fn->second, callee, expr.children, use_location,
                {.infer_expr_type =
                     [](const FunctionScope& nested, const Expr& arg,
                        const SourceLocation* location) {
                         return infer_expr_type_ast(nested, arg, location);
                     },
                 .can_assign =
                     [](const FunctionScope& nested, const std::string& expected, const Expr& value,
                        const std::string& got) {
                         return can_assign_ast(nested, expected, value, got);
                     }})) {
            const FunctionSignature signature =
                instantiate_generic_signature(*generic_fn->second, *type_args);
            check_call_args_ast(scope, callee, signature, expr.children, use_location);
            return signature_return_type_text(signature);
        }
    }
    if (const auto fn = scope.symbols.function_signatures.find(callee);
        fn != scope.symbols.function_signatures.end()) {
        check_call_args_ast(scope, callee, fn->second, expr.children, use_location);
        return signature_return_type_text(fn->second);
    }
    if (const auto signature = native_signature_for_call(
            scope, callee, expr.children, use_location, infer_expr_type_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            })) {
        return signature_return_type_text(*signature);
    }
    if (known_template_constructor_type(scope, callee)) {
        return infer_constructor_call_ast(scope, expr, callee, use_location);
    }
    if (const auto local = scope.locals.find(callee); local != scope.locals.end()) {
        FunctionSignature signature;
        if (parse_local_function_type(scope, callee, local->second, signature)) {
            check_call_args_ast(scope, callee, signature, expr.children, use_location);
            return signature_return_type_text(signature);
        }
    }
    if (is_builtin_call(callee)) {
        return infer_builtin_call_ast(scope, expr, callee, use_location);
    }
    if (!expr.callee.empty() && expr.callee.front().kind == ExprKind::Member &&
        expr.callee.front().children.size() == 1) {
        const Expr& member = expr.callee.front();
        const Expr& receiver_expr = member.children.front();
        if (receiver_expr.kind == ExprKind::Name && receiver_expr.name == "class" &&
            !scope.current_class.empty()) {
            FunctionSignature signature;
            if (static_method_signature_for_type(scope.symbols, scope.current_class, member.name,
                                                 signature, use_location)) {
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature_return_type_text(signature);
            }
        }
        if (receiver_expr.kind == ExprKind::Name && receiver_expr.name != "class" &&
            !scope.locals.contains(receiver_expr.name) &&
            scope.symbols.classes.contains(receiver_expr.name)) {
            FunctionSignature signature;
            if (static_method_signature_for_type(scope.symbols, receiver_expr.name, member.name,
                                                 signature, use_location)) {
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature_return_type_text(signature);
            }
        }
        const bool bare_nonlocal_receiver =
            receiver_expr.kind == ExprKind::Name && !scope.locals.contains(receiver_expr.name);
        if (!bare_nonlocal_receiver) {
            const TypeRef receiver_type_ref =
                infer_expr_type_ast(scope, receiver_expr, use_location);
            const std::string receiver_type = substitute_type_ref_text(receiver_type_ref, {});
            if (receiver_type.empty() || receiver_type == "auto") {
                for (const Expr& arg : expr.children) {
                    check_expr_ast(scope, arg, use_location);
                }
                return "auto";
            }
            if (receiver_type.empty()) {
                return {};
            }
            const bool foreign_receiver =
                foreign_cpp_type_name(scope.symbols, resolve_alias(scope.symbols, receiver_type));
            FunctionSignature signature;
            if (!foreign_receiver) {
                if (const auto inferred = inferred_generic_method_signature_for_type(
                        scope, receiver_type, member.name, expr.children, use_location,
                        {.infer_expr_type =
                             [](const FunctionScope& nested, const Expr& arg,
                                const SourceLocation* location) {
                                 return infer_expr_type_ast(nested, arg, location);
                             },
                         .can_assign =
                             [](const FunctionScope& nested, const std::string& expected,
                                const Expr& value, const std::string& got) {
                                 return can_assign_ast(nested, expected, value, got);
                             }})) {
                    check_call_args_ast(scope, callee, *inferred, expr.children, use_location);
                    return signature_return_type_text(*inferred);
                }
            }
            if (method_signature_for_type(scope.symbols, receiver_type, member.name, signature,
                                          foreign_receiver ? nullptr : use_location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type, member.name);
                if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
                    check_call_args_ast(scope, callee, *match, expr.children, use_location);
                    return signature_return_type_text(*match);
                }
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature_return_type_text(signature);
            }
            if (foreign_receiver) {
                for (const Expr& arg : expr.children) {
                    check_expr_ast(scope, arg, use_location);
                }
                return "auto";
            }
        }
    }
    const size_t method_dot = callee.rfind('.');
    if (method_dot != std::string::npos) {
        if (native_import_path_prefix(scope.symbols, callee)) {
            for (const Expr& arg : expr.children) {
                check_expr_ast(scope, arg, use_location);
            }
            return "auto";
        }
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unknown function: " + callee);
        }
    } else if (!expr.callee.empty() && expr.callee.front().kind != ExprKind::Name) {
        if (use_location != nullptr) {
            sema_expr_fail(*use_location, "unsupported call expression: " + callee);
        }
        return {};
    }
    if (use_location != nullptr) {
        sema_expr_fail(*use_location, "unknown function: " + callee);
    }
    return {};
}

} // namespace dudu
