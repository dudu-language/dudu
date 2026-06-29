#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <vector>

namespace dudu {

void lint_suspicious_cast_module(const ModuleAst& module, const Document& doc,
                                 std::vector<Diagnostic>& out);

} // namespace dudu
