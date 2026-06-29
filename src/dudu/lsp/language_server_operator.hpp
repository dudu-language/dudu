#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

std::optional<Symbol> dudu_operator_symbol_for_expr(const ModuleAst& module, const Expr& expr,
                                                    int one_based_line);
bool dudu_operator_query_exists(const ModuleAst& module, const std::string& query);

} // namespace dudu
