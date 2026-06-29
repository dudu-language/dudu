#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <vector>

namespace dudu {

std::vector<Diagnostic> ast_lint_diagnostics(const ModuleAst& module, const Document& doc);

} // namespace dudu
