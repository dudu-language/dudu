#pragma once

#include "dudu/ast.hpp"

#include <iosfwd>
#include <string>
#include <string_view>

namespace dudu {

bool cpp_emit_function_has_decorator(const FunctionDecl& fn, std::string_view name);
bool cpp_emit_function_is_test(const FunctionDecl& fn);
std::string cpp_emit_string_literal(std::string text);
std::string cpp_emit_function_decorator_arg(const FunctionDecl& fn, std::string_view name);

void emit_test_harness(std::ostringstream& out, const ModuleAst& module, const std::string& filter,
                       bool capture_output);

} // namespace dudu
