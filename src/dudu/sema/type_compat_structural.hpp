#pragma once

#include "dudu/core/ast.hpp"

namespace dudu {

bool structural_type_assignment_allowed(const TypeRef& expected, const TypeRef& got);

} // namespace dudu
