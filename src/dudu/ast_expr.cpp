#include "dudu/ast_expr.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <sstream>

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

std::string join_display_exprs(const std::vector<Expr>& exprs, std::string_view separator) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << display_expr(exprs[i]);
    }
    return out.str();
}

std::string display_template_args(const Expr& expr) {
    std::ostringstream out;
    bool first = true;
    for (const TypeRef& type : expr.template_type_args) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << substitute_type_ref_text(type, {});
    }
    for (const Expr& value : expr.template_args) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << display_expr(value);
    }
    return out.str();
}

std::string display_call_expr(const Expr& expr) {
    std::string callee = call_callee_text(expr);
    if (callee.empty() && !expr.callee.empty()) {
        callee = display_expr(expr.callee.front());
    }
    return callee + "(" + join_display_exprs(expr.children, ", ") + ")";
}

std::string display_expr(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::Name:
        return expr.name;
    case ExprKind::BoolLiteral:
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::StringLiteral:
        return expr.text;
    case ExprKind::NoneLiteral:
        return "None";
    case ExprKind::Unary:
        return expr.children.empty() ? expr.op : expr.op + display_expr(expr.children.front());
    case ExprKind::Binary:
        return expr.children.size() == 2 ? display_expr(expr.children[0]) + " " + expr.op + " " +
                                               display_expr(expr.children[1])
                                         : trim_copy(expr.text);
    case ExprKind::Call:
        return display_call_expr(expr);
    case ExprKind::TemplateCall:
        return call_callee_text(expr) + "[" + display_template_args(expr) + "](" +
               join_display_exprs(expr.children, ", ") + ")";
    case ExprKind::Member:
        return expr.children.size() == 1 ? display_expr(expr.children.front()) + "." + expr.name
                                         : trim_copy(expr.text);
    case ExprKind::Index:
        return expr.children.size() == 2
                   ? display_expr(expr.children[0]) + "[" + display_expr(expr.children[1]) + "]"
                   : trim_copy(expr.text);
    case ExprKind::ListLiteral:
        return "[" + join_display_exprs(expr.children, ", ") + "]";
    case ExprKind::SetLiteral:
        return "{" + join_display_exprs(expr.children, ", ") + "}";
    case ExprKind::TupleLiteral:
        return "(" + join_display_exprs(expr.children, ", ") + ")";
    case ExprKind::DictEntry:
        return expr.children.size() == 2
                   ? display_expr(expr.children[0]) + ": " + display_expr(expr.children[1])
                   : trim_copy(expr.text);
    case ExprKind::DictLiteral:
        return "{" + join_display_exprs(expr.children, ", ") + "}";
    case ExprKind::NamedArg:
        return expr.children.size() == 1 ? expr.name + "=" + display_expr(expr.children.front())
                                         : trim_copy(expr.text);
    case ExprKind::Slice:
        if (expr.children.empty()) {
            return ":";
        }
        if (expr.children.size() == 2) {
            return (expr_missing(expr.children[0]) ? "" : display_expr(expr.children[0])) + ":" +
                   (expr_missing(expr.children[1]) ? "" : display_expr(expr.children[1]));
        }
        return trim_copy(expr.text);
    case ExprKind::Conditional:
    case ExprKind::DefExpression:
    case ExprKind::Lambda:
    case ExprKind::Await:
    case ExprKind::Yield:
    case ExprKind::CppEscape:
    case ExprKind::Unknown:
        return trim_copy(expr.text);
    }
    return trim_copy(expr.text);
}

} // namespace dudu
