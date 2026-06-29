#pragma once

#include "dudu/core/ast.hpp"

#include <string>

namespace dudu {

std::string semantic_tokens_json(const ModuleAst& module);
std::string semantic_tokens_json(const ModuleAst& module, const ModuleAst& native_symbols);

} // namespace dudu
