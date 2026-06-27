#pragma once

#include "dudu/ast.hpp"
#include "dudu/language_server_types.hpp"

#include <vector>

namespace dudu {

void lint_scope_module(const ModuleAst& module, const Document& doc, std::vector<Diagnostic>& out);

} // namespace dudu
