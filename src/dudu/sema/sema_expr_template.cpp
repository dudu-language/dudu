#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_dudu_overloads.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_generics.hpp"

namespace dudu {
std::optional<FunctionSignature> explicit_generic_function_signature_ast(
    const FunctionScope& scope, const Expr& expr, const std::string& callee_base,
    const std::string& emitted_callee, const SourceLocation* location) {
    const auto overloads = scope.symbols.function_overload_decls.find(callee_base);
    if (overloads == scope.symbols.function_overload_decls.end()) {
        return std::nullopt;
    }
    const std::vector<TypeRef> type_args = template_type_refs(expr);
    if (location != nullptr) {
        for (const TypeRef& type_arg : type_args) {
            if (!explicit_generic_arg_known(scope.symbols, type_arg)) {
                const auto unknown = unknown_type_ref(scope.symbols, type_arg);
                const SourceLocation type_location =
                    unknown && unknown->second.line > 0 ? unknown->second : type_arg.location;
                sema_expr_fail(type_location,
                               "unknown generic argument type: " +
                                   (unknown ? unknown->first : type_ref_text(type_arg)));
            }
        }
    }
    const auto selected = select_dudu_function_overload(scope, callee_base, expr.children,
                                                        overloads->second, type_args);
    if (selected) {
        check_call_args_ast(scope, emitted_callee, selected->signature, expr.children, location);
        if (location != nullptr) {
            check_instantiated_generic_function_body(scope, *selected->declaration,
                                                     selected->generic_args, "", *location);
        }
        return selected->signature;
    }
    if (location != nullptr) {
        for (const FunctionDecl* declaration : overloads->second) {
            if (declaration != nullptr && !declaration->generic_params.empty() &&
                generic_arity_matches(declaration->generic_params, type_args.size())) {
                const FunctionSignature signature =
                    instantiate_generic_signature(*declaration, type_args);
                check_call_args_ast(scope, emitted_callee, signature, expr.children, location);
            }
        }
        sema_expr_fail(*location, "no matching overload for " + emitted_callee);
    }
    return std::nullopt;
}

} // namespace dudu
