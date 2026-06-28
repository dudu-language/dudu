#pragma once

#include "dudu/ast.hpp"
#include "dudu/ast_type.hpp"

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
    for (const Expr& callee : expr.callee) {
        visit_expr_tree_impl(callee, visit_expr, visit_type);
    }
    for (const Expr& param : expr.params) {
        visit_expr_tree_impl(param, visit_expr, visit_type);
    }
    for (const Expr& arg : expr.template_args) {
        visit_expr_tree_impl(arg, visit_expr, visit_type);
    }
    for (const TypeRef& arg : expr.template_type_args) {
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

template <typename Add> void visit_stmt_binding_names(const Stmt& stmt, Add add) {
    if ((stmt.kind == StmtKind::VarDecl || stmt.kind == StmtKind::For ||
         stmt.kind == StmtKind::Except) &&
        !stmt.name.empty()) {
        add(stmt.name, stmt.location);
        return;
    }
    if (stmt.kind != StmtKind::Assign) {
        return;
    }
    if (stmt.target_expr.kind == ExprKind::Name && !stmt.target_expr.name.empty()) {
        add(stmt.target_expr.name, stmt.target_expr.location);
        return;
    }
    if (stmt.target_expr.kind == ExprKind::TupleLiteral) {
        for (const Expr& child : stmt.target_expr.children) {
            if (child.kind == ExprKind::Name && !child.name.empty()) {
                add(child.name, child.location);
            }
        }
    }
}

} // namespace dudu
