#include "dudu/unsupported.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema.hpp"

#include <string>

namespace dudu {
namespace {

std::string unsupported_statement_message(UnsupportedFeature feature) {
    switch (feature) {
    case UnsupportedFeature::Exceptions:
        return "unsupported Python feature: finally; use RAII object lifetime or explicit cleanup";
    case UnsupportedFeature::Generators:
        return "unsupported Python feature: generators; use an explicit iterator/state type or "
               "callback";
    case UnsupportedFeature::Async:
        return "unsupported Python feature: async; use explicit callbacks, threads, or imported "
               "native async APIs";
    case UnsupportedFeature::ContextManagers:
        return "unsupported Python feature: with; rely on RAII object lifetime or write the "
               "lifetime explicitly";
    case UnsupportedFeature::GlobalRebinding:
        return "unsupported Python feature: global rebinding; pass state explicitly or use "
               "module/class state";
    case UnsupportedFeature::NonlocalRebinding:
        return "unsupported Python feature: nonlocal rebinding; pass state explicitly";
    case UnsupportedFeature::DynamicDeletion:
        return "unsupported Python feature: del; use explicit container removal or delete/free "
               "for owned native memory";
    case UnsupportedFeature::LocalFunctionDeclarations:
        return "unsupported Python feature: local def; move the function to top-level or class "
               "scope, then pass the function name as a value";
    case UnsupportedFeature::LocalImports:
        return "unsupported Python feature: local imports; keep imports at module scope";
    case UnsupportedFeature::None:
        return "unsupported Python feature";
    }
    return "unsupported Python feature";
}

void check_expr(const Expr& expr) {
    if (expr_missing(expr)) {
        return;
    }
    if (expr.kind == ExprKind::Unknown) {
        return;
    }
    if (expr.kind == ExprKind::DefExpression) {
        throw CompileError(expr.location,
                           "unsupported Python feature: def expressions; move the function to "
                           "top-level or class scope, then pass the function name as a value");
    }
    if (expr.kind == ExprKind::Comprehension) {
        throw CompileError(expr.location,
                           "unsupported Python feature: comprehensions; use an explicit loop "
                           "and append/insert into a declared container");
    }
    if (expr.kind == ExprKind::Await) {
        throw CompileError(expr.location,
                           "unsupported Python feature: async; use explicit callbacks, threads, "
                           "or imported native async APIs");
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
        throw CompileError(expr.location, "unsupported Python feature: generators; use an explicit "
                                          "iterator/state type or callback");
    }
    if (expr.kind == ExprKind::Call) {
        const std::optional<std::string> callee = bare_callee_name(expr);
        if (callee == "eval" || callee == "exec") {
            throw CompileError(expr.location,
                               "unsupported Python feature: dynamic execution is not part of Dudu");
        }
        if (callee == "getattr" || callee == "setattr") {
            throw CompileError(expr.location,
                               "unsupported Python feature: dynamic attribute access; use "
                               "statically known fields or methods");
        }
    }
    for (const Expr& child : expr.callee) {
        check_expr(child);
    }
    for (const Expr& child : expr_template_args(expr)) {
        check_expr(child);
    }
    for (const Expr& child : expr.children) {
        check_expr(child);
    }
}

void check_statement(const Stmt& stmt) {
    if (stmt.kind == StmtKind::Unsupported) {
        throw CompileError(stmt.location, unsupported_statement_message(stmt.unsupported_feature));
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
