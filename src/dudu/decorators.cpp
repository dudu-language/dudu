#include "dudu/decorators.hpp"

#include "dudu/ast_expr.hpp"

#include <sstream>

namespace dudu {
namespace {

std::string expr_path(const Expr& expr) {
    if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
        return render_expr_path(*path);
    }
    return display_expr(expr);
}

} // namespace

std::string decorator_name(const Decorator& decorator) {
    const Expr& expr = decorator.expr;
    if ((expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
        !expr.callee.empty()) {
        return expr_path(expr.callee.front());
    }
    return expr_path(expr);
}

bool decorator_matches(const Decorator& decorator, std::string_view name) {
    return decorator_name(decorator) == name;
}

bool has_decorator(const std::vector<Decorator>& decorators, std::string_view name) {
    for (const Decorator& decorator : decorators) {
        if (decorator_matches(decorator, name)) {
            return true;
        }
    }
    return false;
}

bool decorator_call_matches(const Decorator& decorator, std::string_view name) {
    return decorator.expr.kind == ExprKind::Call && decorator_matches(decorator, name);
}

std::optional<std::string> decorator_first_arg_display(const Decorator& decorator,
                                                       std::string_view name) {
    if (!decorator_call_matches(decorator, name) || decorator.expr.children.empty()) {
        return std::nullopt;
    }
    return display_expr(decorator.expr.children.front());
}

std::optional<std::string> decorator_arg_list_display(const Decorator& decorator,
                                                      std::string_view name) {
    if (!decorator_call_matches(decorator, name)) {
        return std::nullopt;
    }
    std::ostringstream out;
    for (size_t i = 0; i < decorator.expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << display_expr(decorator.expr.children[i]);
    }
    return out.str();
}

std::optional<std::string> decorator_first_string_arg(const Decorator& decorator,
                                                      std::string_view name) {
    return decorator_first_string_literal_arg(decorator, name);
}

std::optional<std::string> decorator_first_string_literal_arg(const Decorator& decorator,
                                                              std::string_view name) {
    if (!decorator_call_matches(decorator, name) || decorator.expr.children.empty()) {
        return std::nullopt;
    }
    const Expr& arg = decorator.expr.children.front();
    if (arg.kind == ExprKind::StringLiteral) {
        return arg.value;
    }
    return std::nullopt;
}

bool decorator_has_single_string_literal_arg(const Decorator& decorator, std::string_view name) {
    return decorator_call_matches(decorator, name) && decorator.expr.children.size() == 1 &&
           decorator.expr.children.front().kind == ExprKind::StringLiteral;
}

} // namespace dudu
