#include "dudu/sema_expr_cpp_escape_calls.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
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

TypeRef call_callee_type_ref(const Expr& parsed, const std::string& callee) {
    if (callee.empty()) {
        return {};
    }
    if (parsed.kind == ExprKind::TemplateCall && !parsed.template_type_args.empty()) {
        TypeRef type;
        type.kind = TypeKind::Template;
        type.name = callee;
        type.children = parsed.template_type_args;
        type.location = parsed.location;
        type.range = parsed.range;
        return type;
    }
    const SourceLocation location =
        parsed.callee.empty() ? parsed.location : parsed.callee.front().location;
    return named_type_ref(callee, location);
}

} // namespace

std::optional<EscapeCall> parsed_escape_call(const Expr& parsed) {
    if (parsed.kind != ExprKind::Call && parsed.kind != ExprKind::TemplateCall) {
        return std::nullopt;
    }
    std::string callee = direct_callee_name(parsed);
    if (callee.empty()) {
        return std::nullopt;
    }
    TypeRef callee_type = call_callee_type_ref(parsed, callee);
    return EscapeCall{.callee = std::move(callee),
                      .callee_expr = parsed.callee.empty() ? Expr{} : parsed.callee.front(),
                      .callee_type_ref = std::move(callee_type),
                      .args = parsed.children};
}

std::optional<EscapeCall> parse_cpp_escape_call_text(const std::string& expr, size_t open,
                                                     SourceLocation location) {
    if (find_call_close(expr, open) != expr.size() - 1) {
        return std::nullopt;
    }
    const std::string callee = trim(expr.substr(0, open));
    return EscapeCall{.callee = callee,
                      .callee_expr = Expr{},
                      .callee_type_ref = named_type_ref(callee, location),
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
