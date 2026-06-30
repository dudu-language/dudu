#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <map>
#include <optional>
#include <string>

namespace dudu {

struct Json;
struct ModuleAst;

std::optional<std::string>
declaration_references_json(const Document& doc, const ModuleAst& current, const Json* params,
                            const std::map<std::string, Document>& workspace);

} // namespace dudu
