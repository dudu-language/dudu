#pragma once

#include "dudu/core/ast.hpp"

namespace dudu {
namespace detail {

template <typename Visit> void visit_expr_tree_impl(const Expr& expr, Visit& visit) {
    visit(expr);
    if (expr.callee != nullptr) {
        for (const Expr& child : *expr.callee) {
            visit_expr_tree_impl(child, visit);
        }
    }
    if (expr.template_args != nullptr) {
        for (const Expr& child : *expr.template_args) {
            visit_expr_tree_impl(child, visit);
        }
    }
    for (const Expr& child : expr.children) {
        visit_expr_tree_impl(child, visit);
    }
}

template <typename Visit> void visit_stmt_expressions_impl(const Stmt& stmt, Visit& visit) {
    visit(stmt.expr);
    visit(stmt.value_expr);
    if (stmt.target_expr != nullptr) {
        visit(*stmt.target_expr);
    }
    if (stmt.condition_expr != nullptr) {
        visit(*stmt.condition_expr);
    }
    if (stmt.message_expr != nullptr) {
        visit(*stmt.message_expr);
    }
    if (stmt.iterable_expr != nullptr) {
        visit(*stmt.iterable_expr);
    }
    if (stmt.pattern_expr != nullptr) {
        visit(*stmt.pattern_expr);
    }
    if (stmt.guard_expr != nullptr) {
        visit(*stmt.guard_expr);
    }
}

template <typename Visit> void visit_stmt_tree_expressions_impl(const Stmt& stmt, Visit& visit) {
    auto visit_expr = [&](const Expr& expr) { visit_expr_tree_impl(expr, visit); };
    visit_stmt_expressions_impl(stmt, visit_expr);
    for (const Stmt& child : stmt.children) {
        visit_stmt_tree_expressions_impl(child, visit);
    }
}

} // namespace detail

template <typename Visit> void visit_expr_tree(const Expr& expr, Visit visit) {
    detail::visit_expr_tree_impl(expr, visit);
}

template <typename Visit> void visit_stmt_expressions(const Stmt& stmt, Visit visit) {
    detail::visit_stmt_expressions_impl(stmt, visit);
}

template <typename Visit> void visit_stmt_tree_expressions(const Stmt& stmt, Visit visit) {
    detail::visit_stmt_tree_expressions_impl(stmt, visit);
}

} // namespace dudu
