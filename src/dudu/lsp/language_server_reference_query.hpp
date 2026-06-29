#pragma once

#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <string>
#include <vector>

namespace dudu {

struct Json;
struct ModuleAst;
struct Symbol;

std::string reference_query_at(const Document& doc, const Json* params,
                               const AstSelection& selection, const ModuleAst* module,
                               const std::vector<Symbol>& symbols_with_native);

} // namespace dudu
