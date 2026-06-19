#pragma once

#include "dudu/ast.hpp"

#include <vector>

namespace dudu {

bool native_function_equivalent(const NativeFunctionDecl& lhs, const NativeFunctionDecl& rhs);
bool contains_equivalent_native_function(const std::vector<NativeFunctionDecl>& functions,
                                         const NativeFunctionDecl& candidate);
void append_unique_native_functions(std::vector<NativeFunctionDecl>& target,
                                    const std::vector<NativeFunctionDecl>& source);

} // namespace dudu
