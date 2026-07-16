#pragma once

#include "dudu/core/ast.hpp"

#include <string>

namespace dudu {

TypeRef normalize_cpp_type_structure_ref(const TypeRef& type);
TypeRef normalize_cpp_type_artifacts_ref(const TypeRef& type);
bool native_associated_type_assignment_allowed(const TypeRef& expected, const TypeRef& got);
bool native_numeric_operator_operand(const TypeRef& type);
bool native_associated_operator_operand_is_dependent(const TypeRef& type);

} // namespace dudu
