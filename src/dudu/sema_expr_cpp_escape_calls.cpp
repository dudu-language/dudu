#include "dudu/sema_expr_cpp_escape_calls.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_scan.hpp"

#include <utility>

namespace dudu {
namespace {

std::vector<Expr> parse_escape_exprs(const std::vector<std::string>& exprs,
                                     SourceLocation location) {
    std::vector<Expr> out;
    out.reserve(exprs.size());
    for (const std::string& expr : exprs) {
        out.push_back(parse_expr_text(expr, location));
    }
    return out;
}

std::vector<Expr> call_arg_exprs(std::string expr, size_t open, SourceLocation location) {
    std::string args = trim(expr.substr(open + 1, expr.size() - open - 2));
    return parse_escape_exprs(
        args.empty() ? std::vector<std::string>{} : split_top_level_args(args), location);
}

} // namespace

std::optional<EscapeCall> parsed_escape_call(const Expr& parsed) {
    if (parsed.kind != ExprKind::Call) {
        return std::nullopt;
    }
    std::string callee = direct_callee_name(parsed);
    if (callee.empty()) {
        return std::nullopt;
    }
    return EscapeCall{.callee = std::move(callee),
                      .callee_expr = parsed.callee.empty() ? Expr{} : parsed.callee.front(),
                      .args = parsed.children};
}

std::optional<EscapeCall> escape_call_from_text(const std::string& expr, size_t open,
                                                SourceLocation location) {
    if (find_call_close(expr, open) != expr.size() - 1) {
        return std::nullopt;
    }
    return EscapeCall{.callee = trim(expr.substr(0, open)),
                      .callee_expr = Expr{},
                      .args = call_arg_exprs(expr, open, location)};
}

std::optional<EscapeMemberCall> parsed_member_call(const EscapeCall& call) {
    if (call.callee_expr.kind != ExprKind::Member || call.callee_expr.children.size() != 1 ||
        call.callee_expr.name.empty()) {
        return std::nullopt;
    }
    const Expr& receiver = call.callee_expr.children.front();
    const auto path = expr_path_from_expr(receiver);
    if (!path) {
        return std::nullopt;
    }
    return EscapeMemberCall{.receiver = render_expr_path(*path),
                            .method = call.callee_expr.name,
                            .receiver_expr = receiver};
}

} // namespace dudu
