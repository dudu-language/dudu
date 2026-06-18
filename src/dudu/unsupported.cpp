#include "dudu/unsupported.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema.hpp"

namespace dudu {
namespace {

void check_expr(const Expr& expr) {
    if (expr_missing(expr)) {
        return;
    }
    if (expr.kind == ExprKind::Unknown) {
        return;
    }
    if (expr.kind == ExprKind::DefExpression) {
        throw CompileError(expr.location, "unsupported Python feature: def expressions");
    }
    if (expr.kind == ExprKind::Comprehension) {
        throw CompileError(expr.location, "unsupported Python feature: comprehensions");
    }
    if (expr.kind == ExprKind::Await) {
        throw CompileError(expr.location, "unsupported Python feature: async");
    }
    if (expr.kind == ExprKind::Lambda) {
        throw CompileError(expr.location,
                           "unsupported Python feature: lambda; declare a named function and "
                           "pass the function name");
    }
    if (expr.kind == ExprKind::Conditional) {
        throw CompileError(expr.location,
                           "unsupported Python feature: conditional expressions; use an "
                           "explicit if statement");
    }
    if (expr.kind == ExprKind::Yield) {
        throw CompileError(expr.location, "unsupported Python feature: generators");
    }
    if (expr.kind == ExprKind::Call) {
        const std::optional<std::string> callee = bare_callee_name(expr);
        if (callee == "eval" || callee == "exec") {
            throw CompileError(expr.location, "unsupported Python feature: dynamic execution");
        }
        if (callee == "getattr" || callee == "setattr") {
            throw CompileError(expr.location,
                               "unsupported Python feature: dynamic attribute access");
        }
    }
    for (const Expr& child : expr.callee) {
        check_expr(child);
    }
    for (const Expr& child : expr.params) {
        check_expr(child);
    }
    for (const Expr& child : expr.template_args) {
        check_expr(child);
    }
    for (const Expr& child : expr.children) {
        check_expr(child);
    }
}

void check_statement(const Stmt& stmt) {
    if (stmt.kind == StmtKind::Unsupported) {
        throw CompileError(stmt.location,
                           "unsupported Python feature: " +
                               std::string(unsupported_feature_name(stmt.unsupported_feature)));
    }
    if (stmt.kind == StmtKind::Unknown) {
        throw CompileError(stmt.location, "unsupported statement kind: " +
                                              std::string(statement_kind_name(stmt.kind)));
    } else {
        visit_stmt_expressions(stmt, check_expr);
    }
    for (const Stmt& child : stmt.children) {
        check_statement(child);
    }
}

} // namespace

void check_unsupported_python(const ModuleAst& module) {
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            for (const Stmt& stmt : method.statements) {
                check_statement(stmt);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        for (const Stmt& stmt : fn.statements) {
            check_statement(stmt);
        }
    }
}

} // namespace dudu
