#include "dudu/ast_expr.hpp"

#include "dudu/cpp_lower.hpp"

namespace dudu {

std::optional<std::string> member_path_from_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        return expr.name;
    }
    if (expr.kind == ExprKind::Member && expr.children.size() == 1 && !expr.name.empty()) {
        const std::optional<std::string> receiver = member_path_from_expr(expr.children.front());
        if (receiver.has_value()) {
            return *receiver + "." + expr.name;
        }
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2) {
        const std::optional<std::string> receiver = member_path_from_expr(expr.children.front());
        if (receiver.has_value() && !expr.children[1].text.empty()) {
            return *receiver + "[" + expr.children[1].text + "]";
        }
    }
    return std::nullopt;
}

bool is_member_callee(const Expr& expr, std::string_view receiver, std::string_view member) {
    if (expr.kind != ExprKind::Call || expr.callee.size() != 1) {
        return false;
    }
    const Expr& callee = expr.callee.front();
    if (callee.kind != ExprKind::Member || callee.name != member || callee.children.size() != 1) {
        return false;
    }
    const Expr& receiver_expr = callee.children.front();
    return receiver_expr.kind == ExprKind::Name && receiver_expr.name == receiver;
}

std::string call_callee_text(const Expr& expr) {
    if (!expr.callee.empty()) {
        if (const std::optional<std::string> path = member_path_from_expr(expr.callee.front())) {
            return *path;
        }
    }
    return trim_copy(expr.name);
}

} // namespace dudu
