#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"

#include <algorithm>

namespace dudu {

namespace detail {
template <typename Visit> void visit_type_ref_tree_impl(const TypeRef& type, Visit& visit) {
    if (!has_type_ref(type)) {
        return;
    }
    visit(type);
    for (const TypeRef& child : type.children) {
        visit_type_ref_tree_impl(child, visit);
    }
}

template <typename VisitExpr, typename VisitType>
void visit_expr_tree_impl(const Expr& expr, VisitExpr& visit_expr, VisitType& visit_type) {
    visit_expr(expr);
    for (const Expr& callee : expr_callee(expr)) {
        visit_expr_tree_impl(callee, visit_expr, visit_type);
    }
    for (const Expr& arg : expr_template_args(expr)) {
        visit_expr_tree_impl(arg, visit_expr, visit_type);
    }
    for (const TypeRef& arg : expr_template_type_args(expr)) {
        visit_type_ref_tree_impl(arg, visit_type);
    }
    if (has_expr_type_ref(expr)) {
        visit_type_ref_tree_impl(expr_type_ref(expr), visit_type);
    }
    for (const Expr& child : expr.children) {
        visit_expr_tree_impl(child, visit_expr, visit_type);
    }
}
} // namespace detail

template <typename Visit> void visit_type_ref_tree(const TypeRef& type, Visit visit) {
    detail::visit_type_ref_tree_impl(type, visit);
}

template <typename VisitExpr, typename VisitType>
void visit_lsp_expr_tree(const Expr& expr, VisitExpr visit_expr, VisitType visit_type) {
    detail::visit_expr_tree_impl(expr, visit_expr, visit_type);
}

template <typename Visit>
void visit_lsp_stmt_tree(const std::vector<Stmt>& statements, Visit visit) {
    for (const Stmt& stmt : statements) {
        visit(stmt);
        visit_lsp_stmt_tree(stmt.children, visit);
    }
}

inline int statement_tree_end_line(const std::vector<Stmt>& statements) {
    int line = 0;
    visit_lsp_stmt_tree(statements, [&](const Stmt& stmt) {
        line = std::max(line, stmt.location.line);
        line = std::max(line, stmt.range.end.line);
    });
    return line;
}

inline int statement_source_end_line(const Stmt& statement) {
    return std::max(std::max(statement.location.line, statement.range.end.line),
                    statement_tree_end_line(statement.children));
}

inline bool statement_contains_source_line(const Stmt& statement, int one_based_line) {
    return statement.location.line <= one_based_line &&
           one_based_line <= statement_source_end_line(statement);
}

inline int function_source_end_line(const FunctionDecl& function) {
    return std::max(std::max(function.location.line, function.range.end.line),
                    statement_tree_end_line(function.statements));
}

inline bool function_contains_source_line(const FunctionDecl& function, int one_based_line) {
    return function.location.line <= one_based_line &&
           one_based_line <= function_source_end_line(function);
}

template <typename Add> void visit_pattern_binding_names(const Expr& pattern, Add add) {
    if (pattern.kind != ExprKind::Call) {
        return;
    }
    for (const Expr& child : pattern.children) {
        const Expr* binding = &child;
        if (child.kind == ExprKind::NamedArg && child.children.size() == 1) {
            binding = &child.children.front();
        }
        if (binding->kind == ExprKind::Name && !binding->name.empty() && binding->name != "_") {
            add(binding->name, binding->location);
        }
    }
}

template <typename Add> void visit_stmt_binding_names(const Stmt& stmt, Add add) {
    if ((stmt.kind == StmtKind::VarDecl || stmt.kind == StmtKind::For ||
         stmt.kind == StmtKind::Except) &&
        !stmt.name.empty()) {
        add(stmt.name, stmt.location);
        return;
    }
    if (stmt.kind == StmtKind::Case) {
        visit_pattern_binding_names(stmt_pattern_expr(stmt), add);
        return;
    }
    if (stmt.kind != StmtKind::Assign) {
        return;
    }
    if (stmt_target_expr(stmt).kind == ExprKind::Name && !stmt_target_expr(stmt).name.empty()) {
        add(stmt_target_expr(stmt).name, stmt_target_expr(stmt).location);
        return;
    }
    if (stmt_target_expr(stmt).kind == ExprKind::TupleLiteral) {
        for (const Expr& child : stmt_target_expr(stmt).children) {
            if (child.kind == ExprKind::Name && !child.name.empty()) {
                add(child.name, child.location);
            }
        }
    }
}

} // namespace dudu
