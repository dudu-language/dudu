#pragma once

#include "dudu/core/ast.hpp"

#include <vector>

namespace dudu {

std::vector<bool> cpp_public_import_mask(const ModuleAst& module);

} // namespace dudu
