#include "dudu/core/ast_expr.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_expr_internal.hpp"

namespace dudu {
std::optional<FunctionSignature> explicit_generic_function_signature_ast(
    const FunctionScope& scope, const Expr& expr, const std::string& callee_base,
    const std::string& emitted_callee, const SourceLocation* location) {
    const auto fn = scope.symbols.function_decls.find(callee_base);
    if (fn == scope.symbols.function_decls.end() || fn->second->generic_params.empty()) {
        return std::nullopt;
    }
    const std::vector<TypeRef> type_args = template_type_refs(expr);
    if (location != nullptr && type_args.size() != fn->second->generic_params.size()) {
        sema_expr_fail(*location, "function " + callee_base + " expects " +
                                      std::to_string(fn->second->generic_params.size()) +
                                      " type arguments, got " + std::to_string(type_args.size()));
    }
    if (location != nullptr) {
        for (const TypeRef& type_arg : type_args) {
            if (const auto unknown = unknown_type_ref(scope.symbols, type_arg)) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : type_arg.location;
                sema_expr_fail(type_location, "unknown generic argument type: " + unknown->first);
            }
        }
    }
    FunctionSignature signature = instantiate_generic_signature(*fn->second, type_args);
    check_call_args_ast(scope, emitted_callee, signature, expr.children, location);
    if (location != nullptr) {
        check_instantiated_generic_function_body(scope, *fn->second, type_args, "", *location);
    }
    return signature;
}

} // namespace dudu
