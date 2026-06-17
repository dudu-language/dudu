#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

std::string normalize_cpp_type_artifacts(const TypeRef& type);
std::string normalize_cpp_type_artifacts(std::string type);

} // namespace dudu
