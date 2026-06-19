#pragma once

#include "dudu/ast.hpp"

namespace dudu {

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
