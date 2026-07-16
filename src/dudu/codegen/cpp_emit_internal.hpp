#pragma once

#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::string cpp_emit_string_literal(std::string text);
std::string cpp_emit_function_decorator_arg(const FunctionDecl& fn, std::string_view name);
bool cpp_emit_concrete_variadic_param(const FunctionDecl& fn, const ParamDecl& param);
std::string cpp_emit_concrete_variadic_pack_name(const ParamDecl& param);
std::vector<std::string> cpp_emit_template_params_for_function(const FunctionDecl& fn);

void emit_test_harness(std::ostringstream& out, const ModuleAst& module, const std::string& filter,
                       bool capture_output, const CppEmitOptions& options = {});

} // namespace dudu
