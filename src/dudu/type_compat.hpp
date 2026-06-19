#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

bool type_assignment_allowed(const TypeRef& expected, const TypeRef& got);
bool assignment_type_allowed(const TypeRef& expected, const Expr& expr, const TypeRef& got);
bool type_ref_is_integer(const TypeRef& type);
std::string display_type(const Expr& expr, const std::string& got);
std::string assignment_error(const TypeRef& expected, const Expr& expr, const std::string& got);
std::string assignment_error(const TypeRef& expected, const Expr& expr, const TypeRef& got);

} // namespace dudu
