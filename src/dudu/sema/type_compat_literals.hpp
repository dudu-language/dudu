#pragma once

#include "dudu/core/ast.hpp"

#include <string>

namespace dudu {

bool parsed_expected_literal_assignment_allowed(const TypeRef& expected, const Expr& expr,
                                                const TypeRef& got);

} // namespace dudu
