#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <vector>

namespace dudu {

std::vector<Symbol> class_member_symbols_for_owner(const ModuleAst& module,
                                                   const std::string& owner);

std::optional<Symbol> class_member_symbol_for_path(const ModuleAst& module, const ExprPath& path);

} // namespace dudu
