#pragma once

#include "dudu/core/ast.hpp"

#include <vector>

namespace dudu {

std::vector<bool> infer_function_generic_param_value_flags(const FunctionDecl& function);

} // namespace dudu
