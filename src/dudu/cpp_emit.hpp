#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

std::string emit_cpp_header(const ModuleAst& module);
std::string emit_cpp_source(const ModuleAst& module);

} // namespace dudu
