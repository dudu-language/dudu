#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

TypeRef normalize_cpp_type_artifacts_ref(const TypeRef& type);
std::string normalize_cpp_type_artifacts(const TypeRef& type);
std::string normalize_cpp_type_artifacts(std::string type);
bool native_associated_type_assignment_allowed(const TypeRef& expected, const TypeRef& got);

} // namespace dudu
