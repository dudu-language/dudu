#include "dudu/cpp_match_emit.hpp"

#include "dudu/control_flow.hpp"
#include "dudu/cpp_expr_emit.hpp"

namespace dudu {
bool match_has_guards(const Stmt& stmt) {
    for (const Stmt& child : stmt.children) {
        if (child.kind == StmtKind::Case && has_expr(child.guard_expr)) {
            return true;
        }
    }
    return false;
}

bool match_cases_return(const Stmt& stmt) {
    if (stmt.children.empty()) {
        return false;
    }
    for (const Stmt& child : stmt.children) {
        if (child.kind != StmtKind::Case || !block_guarantees_return(child.children)) {
            return false;
        }
    }
    return true;
}

} // namespace dudu
