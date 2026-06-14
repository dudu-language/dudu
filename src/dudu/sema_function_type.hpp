#pragma once

#include "dudu/sema_context.hpp"

#include <string>

namespace dudu {

std::string function_type(const FunctionSignature& signature);
bool parse_function_type(const TypeRef& type, FunctionSignature& out);
bool parse_function_type(std::string type, FunctionSignature& out);

} // namespace dudu
