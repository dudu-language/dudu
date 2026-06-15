#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

bool type_assignment_allowed(const std::string& expected, const std::string& got);
bool assignment_type_allowed(const std::string& expected, const Expr& expr, const std::string& got);
std::string display_type(const Expr& expr, const std::string& got);
std::string assignment_error(const std::string& expected, const Expr& expr, const std::string& got);

} // namespace dudu
