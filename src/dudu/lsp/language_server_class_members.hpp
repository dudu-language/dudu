#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <vector>

namespace dudu {

std::vector<Symbol> class_member_symbols_for_owner(const ModuleAst& module,
                                                   const std::string& owner);
std::optional<Symbol> class_member_symbol_for_owner(const ModuleAst& module,
                                                    const std::string& owner,
                                                    const std::string& member);

std::optional<Symbol> class_member_symbol_for_path(const ModuleAst& module, const ExprPath& path);
std::optional<Symbol> class_member_symbol_for_super(const ModuleAst& module, int one_based_line,
                                                    const std::string& member);

} // namespace dudu
