#pragma once

#include "dudu/ast.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

enum class ExprPathSegmentKind {
    Name,
    Index,
};

struct ExprPathSegment {
    ExprPathSegmentKind kind = ExprPathSegmentKind::Name;
    std::string text;
    SourceLocation location;
};

struct ExprPath {
    std::vector<ExprPathSegment> segments;
};

std::optional<ExprPath> expr_path_from_expr(const Expr& expr);
std::string render_expr_path(const ExprPath& path);
std::string expr_label(const Expr& expr);
std::optional<std::string> path_index_from_expr(const Expr& expr);
bool expr_missing(const Expr& expr);
bool expr_present(const Expr& expr);
std::optional<std::string> bare_callee_name(const Expr& expr);
std::optional<std::string> member_callee_name(const Expr& expr);
bool is_member_callee(const Expr& expr, std::string_view receiver, std::string_view member);
std::optional<ExprPath> call_callee_path(const Expr& expr);
std::string direct_callee_name(const Expr& expr);
std::string display_expr(const Expr& expr);

} // namespace dudu
