#include "dudu/sema/sema_constexpr.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/decorators.hpp"

#include <set>

namespace dudu {
namespace {

bool has_constexpr_decorator(const FunctionDecl& fn) {
    return has_decorator(fn, "constexpr");
}

void check_expr_call(const Expr& expr, const std::set<std::string>& constexpr_functions,
                     const std::set<std::string>& dudu_functions,
                     const std::string& class_name = {}) {
    if (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) {
        std::string callee = direct_callee_name(expr);
        if (!class_name.empty() && callee.starts_with("self.")) {
            callee = class_name + callee.substr(4);
        }
        if (dudu_functions.contains(callee) && !constexpr_functions.contains(callee)) {
            throw CompileError(expr.location,
                               "compile-time expression calls non-constexpr function: " + callee);
        }
    }
}

void check_expr_calls(const Expr& expr, const std::set<std::string>& constexpr_functions,
                      const std::set<std::string>& dudu_functions,
                      const std::string& class_name = {}) {
    check_expr_call(expr, constexpr_functions, dudu_functions, class_name);
    for (const Expr& child : expr_callee(expr)) {
        check_expr_calls(child, constexpr_functions, dudu_functions, class_name);
    }
    for (const Expr& child : expr_template_args(expr)) {
        check_expr_calls(child, constexpr_functions, dudu_functions, class_name);
    }
    for (const Expr& child : expr.children) {
        check_expr_calls(child, constexpr_functions, dudu_functions, class_name);
    }
}

void check_constexpr_body(const FunctionDecl& fn, const std::set<std::string>& constexpr_functions,
                          const std::set<std::string>& dudu_functions,
                          const std::string& class_name = {}) {
    if (!has_constexpr_decorator(fn)) {
        return;
    }
    for (const Stmt& stmt : fn.statements) {
        visit_stmt_tree_expressions(stmt, [&](const Expr& expr) {
            check_expr_call(expr, constexpr_functions, dudu_functions, class_name);
        });
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
    for (const FunctionDecl& fn : module.functions) {
        check_constexpr_body(fn, constexpr_functions, functions);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            check_constexpr_body(method, constexpr_functions, functions, klass.name);
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
