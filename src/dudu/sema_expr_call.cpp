#include "dudu/ast_type.hpp"
#include "dudu/sema_expr_internal.hpp"

namespace dudu {
std::string infer_call_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* use_location) {
    const ScopedCallee scoped_callee = scoped_call_callee(scope, expr, use_location);
    const std::string& callee = scoped_callee.key;
    if (callee.empty()) {
        return {};
    }
    if (const auto type = direct_call_type_ref(scope, expr, use_location)) {
        return substitute_type_ref_text(*type, {});
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
