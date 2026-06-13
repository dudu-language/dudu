#include "dudu/sema_expr.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/sema_scan.hpp"

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

} // namespace

bool is_string_literal_expr(const std::string& expr) {
    return expr.size() >= 2 && ((expr.front() == '"' && expr.back() == '"') ||
                                (expr.front() == '\'' && expr.back() == '\''));
}

std::optional<std::string> infer_not_expr(const FunctionScope& scope, const std::string& expr,
                                          const SourceLocation* location,
                                          const InferExprFn& infer_expr) {
    if (!starts_with(expr, "not ")) {
        return std::nullopt;
    }
    const std::string got = infer_expr(scope, expr.substr(4), location);
    if (location != nullptr && !got.empty() && got != "bool") {
        fail(*location, "not expects bool, got " + got);
    }
    return "bool";
}

std::optional<std::string> infer_logical_expr(const FunctionScope& scope, const std::string& expr,
                                              const SourceLocation* location,
                                              const InferExprFn& infer_expr) {
    const size_t logical = find_top_level_logical(expr);
    if (logical == std::string::npos) {
        return std::nullopt;
    }
    const std::string op = expr.substr(logical, expr.substr(logical, 3) == "and" ? 3 : 2);
    const std::string left = infer_expr(scope, expr.substr(0, logical), location);
    const std::string right = infer_expr(scope, expr.substr(logical + op.size()), location);
    if (location != nullptr && !left.empty() && left != "bool") {
        fail(*location, op + " expects bool, got " + left);
    }
    if (location != nullptr && !right.empty() && right != "bool") {
        fail(*location, op + " expects bool, got " + right);
    }
    return "bool";
}

std::optional<std::string> infer_comparison_expr(const FunctionScope& scope,
                                                 const std::string& expr,
                                                 const SourceLocation* location,
                                                 const InferExprFn& infer_expr) {
    const size_t comparison = find_top_level_comparison(expr);
    if (comparison == std::string::npos) {
        return std::nullopt;
    }
    const std::string op = top_level_comparison_text(expr, comparison);
    const std::string left = infer_expr(scope, expr.substr(0, comparison), location);
    const std::string right_expr = expr.substr(comparison + op.size());
    const std::string right = infer_expr(scope, right_expr, location);
    if (location != nullptr && !left.empty() && !right.empty() &&
        !comparison_rhs_allowed(scope.symbols, op, left, right_expr, right)) {
        fail(*location, "comparison " + op + " expects " + left + ", got " + right);
    }
    return "bool";
}

} // namespace dudu
