#pragma once

#include "dudu/ast.hpp"

#include <string>

namespace dudu {

std::string semantic_tokens_json(const ModuleAst& module);

} // namespace dudu
