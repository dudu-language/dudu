#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/lsp/language_server_navigation.hpp"

#include <optional>
#include <string>

namespace dudu {

std::optional<std::string> builtin_function_hover_json(const AstSelection& selection,
                                                       const std::string& query,
                                                       const ModuleAst& current,
                                                       const Document& doc, const Json* params);
std::optional<std::string> builtin_member_hover_json(const ExprPath& path, const Json* params,
                                                     const ModuleAst& current);

} // namespace dudu
