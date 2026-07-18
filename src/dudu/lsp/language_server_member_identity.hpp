#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>

namespace dudu {

bool same_member_declaration(const SourceLocation& left, const SourceLocation& right);

std::optional<Symbol> dudu_member_symbol_for_expr(const ModuleAst& module, const Expr& member_expr,
                                                  const SourceLocation& use_location,
                                                  const Expr* call_expr = nullptr);

} // namespace dudu
