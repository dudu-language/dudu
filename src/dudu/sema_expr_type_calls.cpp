#include "dudu/sema_expr_internal.hpp"

namespace dudu {

std::optional<TypeRef> direct_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                            const SourceLocation* location) {
    const std::string callee = scoped_call_callee_text(scope, expr, location);
    if (callee.empty()) {
        return std::nullopt;
    }
    if (const auto decl = scope.symbols.function_decls.find(callee);
        decl != scope.symbols.function_decls.end() && !decl->second->generic_params.empty()) {
        if (const auto type_args = infer_generic_call_type_args(
                scope, *decl->second, callee, expr.children, location,
                {.infer_expr_type =
                     [](const FunctionScope& nested, const Expr& arg,
                        const SourceLocation* arg_location) {
                         return infer_expr_type_ast(nested, arg, arg_location);
                     },
                 .can_assign =
                     [](const FunctionScope& nested, const std::string& expected, const Expr& value,
                        const std::string& got) {
                         return can_assign_ast(nested, expected, value, got);
                     }})) {
            const FunctionSignature signature =
                instantiate_generic_signature(*decl->second, *type_args);
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature_return_type_ref(signature);
        }
        return std::nullopt;
    }
    if (const auto fn = scope.symbols.function_signatures.find(callee);
        fn != scope.symbols.function_signatures.end()) {
        check_call_args_ast(scope, callee, fn->second, expr.children, location);
        return signature_return_type_ref(fn->second);
    }
    if (const auto signature = native_signature_for_call(
            scope, callee, expr.children, location, infer_expr_type_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            })) {
        return signature_return_type_ref(*signature);
    }
    return std::nullopt;
}

std::optional<TypeRef> direct_template_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                                     const SourceLocation* location) {
    const std::string callee = template_call_callee(scope, expr, location);
    const std::string callee_base = scoped_call_callee_text(scope, expr, location);
    if (const auto signature =
            explicit_generic_function_signature_ast(scope, expr, callee_base, callee, location)) {
        return signature_return_type_ref(*signature);
    }
    return std::nullopt;
}

} // namespace dudu
