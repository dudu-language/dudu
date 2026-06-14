#include "dudu/control_flow.hpp"

namespace dudu {
namespace {

bool branch_chain_guarantees_return(const std::vector<Stmt>& body, size_t& index) {
    bool has_else = false;
    bool all_branches_return = block_guarantees_return(body[index].children);
    while (index + 1 < body.size()) {
        const StmtKind next = body[index + 1].kind;
        if (next != StmtKind::Elif && next != StmtKind::Else) {
            break;
        }
        ++index;
        has_else = has_else || next == StmtKind::Else;
        all_branches_return = all_branches_return && block_guarantees_return(body[index].children);
        if (has_else) {
            break;
        }
    }
    return has_else && all_branches_return;
}

} // namespace

bool block_guarantees_return(const std::vector<Stmt>& body) {
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i].kind == StmtKind::Return) {
            return true;
        }
        if (body[i].kind == StmtKind::If && branch_chain_guarantees_return(body, i)) {
            return true;
        }
    }
    return false;
}

} // namespace dudu
