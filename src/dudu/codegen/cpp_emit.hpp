#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/codegen/cpp_emit_options.hpp"

#include <string>

namespace dudu {

std::string emit_cpp_header(const ModuleAst& module);
std::string emit_cpp_header(const ModuleAst& module, const CppEmitOptions& options);
std::string emit_c_header(const ModuleAst& module);
std::string emit_cpp_source(const ModuleAst& module);
std::string emit_cpp_source(const ModuleAst& module, const CppEmitOptions& options);
std::string emit_cpp_test_source(const ModuleAst& module, const std::string& filter = {},
                                 bool capture_output = true);

} // namespace dudu
