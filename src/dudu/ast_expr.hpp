#pragma once

#include "dudu/ast.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace dudu {

std::optional<std::string> member_path_from_expr(const Expr& expr);
std::string direct_callee_name(const Expr& expr);
bool is_member_callee(const Expr& expr, std::string_view receiver, std::string_view member);
std::string call_callee_text(const Expr& expr);

} // namespace dudu
