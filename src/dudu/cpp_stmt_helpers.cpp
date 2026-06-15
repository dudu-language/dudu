#include "dudu/cpp_stmt_helpers.hpp"

#include <string>

namespace dudu {

std::string indent(int depth) {
    return std::string(static_cast<size_t>(depth) * 4, ' ');
}

std::string cpp_string_literal(std::string text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

namespace {

bool is_build_value_expr(const Expr& expr);

bool is_build_member_expr(const Expr& expr) {
    return expr.kind == ExprKind::Member && expr.children.size() == 1 &&
           expr.children.front().kind == ExprKind::Name && expr.children.front().name == "build";
}

bool is_build_only_condition(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::StringLiteral:
        return true;
    case ExprKind::Member:
        return is_build_member_expr(expr);
    case ExprKind::Unary:
        return expr.children.size() == 1 && expr.op == "not" &&
               is_build_only_condition(expr.children.front());
    case ExprKind::Binary:
        if (expr.children.size() != 2) {
            return false;
        }
        if (expr.op == "and" || expr.op == "or" || expr.op == "==" || expr.op == "!=" ||
            expr.op == "<" || expr.op == "<=" || expr.op == ">" || expr.op == ">=") {
            return is_build_value_expr(expr.children[0]) && is_build_value_expr(expr.children[1]);
        }
        return false;
    default:
        return false;
    }
}

bool is_build_value_expr(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::StringLiteral:
        return true;
    case ExprKind::Member:
        return is_build_member_expr(expr);
    case ExprKind::Unary:
        return expr.children.size() == 1 && expr.op == "not" &&
               is_build_value_expr(expr.children.front());
    case ExprKind::Binary:
        return is_build_only_condition(expr);
    default:
        return false;
    }
}

} // namespace

std::string if_keyword_for_condition(const Expr& condition) {
    return is_build_only_condition(condition) ? "if constexpr" : "if";
}

} // namespace dudu
