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
bool has_expr_template_type_args(const Expr& expr);
const std::vector<TypeRef>& expr_template_type_args(const Expr& expr);
void set_expr_template_type_args(Expr& expr, std::vector<TypeRef> args);
bool has_stmt_message_expr(const Stmt& stmt);
const Expr& stmt_message_expr(const Stmt& stmt);
void set_stmt_message_expr(Stmt& stmt, Expr expr);
bool has_stmt_guard_expr(const Stmt& stmt);
const Expr& stmt_guard_expr(const Stmt& stmt);
void set_stmt_guard_expr(Stmt& stmt, Expr expr);
std::string display_expr(const Expr& expr);

} // namespace dudu
