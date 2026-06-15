#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

bool parsed_expected_literal_assignment_allowed(const TypeRef& expected, const Expr& expr,
                                                const std::string& got);

} // namespace dudu
