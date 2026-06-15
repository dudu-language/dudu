#include "dudu/decorators.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"

namespace dudu {
namespace {

std::string unquote(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string expr_path(const Expr& expr) {
    if (const std::optional<std::string> path = member_path_from_expr(expr)) {
        return *path;
    }
    return trim_copy(expr.text);
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

std::optional<std::string> decorator_first_arg_text(const Decorator& decorator,
                                                    std::string_view name) {
    if (!decorator_call_matches(decorator, name) || decorator.expr.children.empty()) {
        return std::nullopt;
    }
    return trim_copy(decorator.expr.children.front().text);
}

std::optional<std::string> decorator_first_string_arg(const Decorator& decorator,
                                                      std::string_view name) {
    const std::optional<std::string> text = decorator_first_arg_text(decorator, name);
    if (!text.has_value()) {
        return std::nullopt;
    }
    const Expr& arg = decorator.expr.children.front();
    if (arg.kind == ExprKind::StringLiteral) {
        return arg.value;
    }
    return unquote(*text);
}

} // namespace dudu
