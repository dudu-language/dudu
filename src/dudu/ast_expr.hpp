#pragma once

#include "dudu/ast.hpp"

#include <optional>
#include <string>

namespace dudu {

std::optional<std::string> member_path_from_expr(const Expr& expr);
std::string call_callee_text(const Expr& expr);

} // namespace dudu
