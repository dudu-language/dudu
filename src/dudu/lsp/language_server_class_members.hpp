#pragma once

#include "dudu/core/ast_expr.hpp"
#include "dudu/lsp/language_server_types.hpp"
#include "dudu/core/ast.hpp"

#include <optional>

namespace dudu {

std::optional<Symbol> class_member_symbol_for_path(const ModuleAst& module, const ExprPath& path);

} // namespace dudu
