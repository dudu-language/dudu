#pragma once

#include "dudu/ast.hpp"

#include <string>
#include <vector>

namespace dudu {

std::string native_function_key(const NativeFunctionDecl& fn);
void append_unique_native_functions(std::vector<NativeFunctionDecl>& target,
                                    const std::vector<NativeFunctionDecl>& source);

} // namespace dudu
