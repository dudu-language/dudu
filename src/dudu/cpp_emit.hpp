#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

std::string emit_cpp_header(const ModuleAst& module);
std::string emit_c_header(const ModuleAst& module);
std::string emit_cpp_source(const ModuleAst& module);
std::string emit_cpp_test_source(const ModuleAst& module, const std::string& filter = {});

} // namespace dudu
