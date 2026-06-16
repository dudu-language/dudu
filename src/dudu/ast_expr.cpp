#include "dudu/ast_expr.hpp"

#include "dudu/cpp_lower.hpp"

namespace dudu {

std::optional<std::string> path_index_from_expr(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::Name:
        return expr.name.empty() ? std::nullopt : std::optional<std::string>{expr.name};
    case ExprKind::IntLiteral:
    case ExprKind::StringLiteral:
        return expr.text.empty() ? std::nullopt : std::optional<std::string>{expr.text};
    case ExprKind::Member:
        return member_path_from_expr(expr);
    case ExprKind::TupleLiteral: {
        std::string out;
        for (const Expr& child : expr.children) {
            const std::optional<std::string> part = path_index_from_expr(child);
            if (!part.has_value()) {
                return std::nullopt;
            }
            if (!out.empty()) {
                out += ", ";
            }
            out += *part;
        }
        return out;
    }
    default:
        return std::nullopt;
    }
}

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
        const std::optional<std::string> index = path_index_from_expr(expr.children[1]);
        if (receiver.has_value() && index.has_value()) {
            return *receiver + "[" + *index + "]";
        }
    }
    return std::nullopt;
}

bool expr_missing(const Expr& expr) {
    return expr.text.empty() || (expr.kind == ExprKind::Unknown && trim_copy(expr.text).empty());
}

bool expr_present(const Expr& expr) {
    return !expr_missing(expr);
}

std::optional<std::string> bare_callee_name(const Expr& expr) {
    if (!expr.callee.empty() && expr.callee.front().kind == ExprKind::Name &&
        !expr.callee.front().name.empty()) {
        return expr.callee.front().name;
    }
    return std::nullopt;
}

std::string direct_callee_name(const Expr& expr) {
    if (const std::optional<std::string> callee = bare_callee_name(expr)) {
        return *callee;
    }
    return trim_copy(expr.name);
}

std::optional<std::string> member_callee_name(const Expr& expr) {
    if (expr.kind != ExprKind::Call || expr.callee.size() != 1) {
        return std::nullopt;
    }
    const Expr& callee = expr.callee.front();
    if (callee.kind != ExprKind::Member || callee.name.empty()) {
        return std::nullopt;
    }
    return callee.name;
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
