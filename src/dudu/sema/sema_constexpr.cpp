#include "dudu/sema/sema_constexpr.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/decorators.hpp"

#include <set>

namespace dudu {
namespace {

bool has_constexpr_decorator(const FunctionDecl& fn) {
    return has_decorator(fn.decorators, "constexpr");
}

void check_expr_calls(const Expr& expr, const std::set<std::string>& constexpr_functions,
                      const std::set<std::string>& dudu_functions) {
    if (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) {
        const std::string callee = direct_callee_name(expr);
        if (dudu_functions.contains(callee) && !constexpr_functions.contains(callee)) {
            throw CompileError(expr.location,
                               "compile-time expression calls non-constexpr function: " + callee);
        }
    }
    for (const Expr& child : expr_callee(expr)) {
        check_expr_calls(child, constexpr_functions, dudu_functions);
    }
    for (const Expr& child : expr_template_args(expr)) {
        check_expr_calls(child, constexpr_functions, dudu_functions);
    }
    for (const Expr& child : expr.children) {
        check_expr_calls(child, constexpr_functions, dudu_functions);
    }
}

} // namespace

void check_constexpr_uses(const ModuleAst& module) {
    std::set<std::string> functions;
    std::set<std::string> constexpr_functions;
    for (const FunctionDecl& fn : module.functions) {
        functions.insert(fn.name);
        if (has_constexpr_decorator(fn)) {
            constexpr_functions.insert(fn.name);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            functions.insert(klass.name + "." + method.name);
            if (has_constexpr_decorator(method)) {
                constexpr_functions.insert(klass.name + "." + method.name);
            }
        }
    }
    for (const ConstDecl& constant : module.constants) {
        check_expr_calls(constant.value_expr, constexpr_functions, functions);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const ConstDecl& constant : klass.constants) {
            check_expr_calls(constant.value_expr, constexpr_functions, functions);
        }
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        check_expr_calls(assertion.expression_expr, constexpr_functions, functions);
    }
}

} // namespace dudu
