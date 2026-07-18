#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

struct Json;
struct ModuleAst;

std::optional<std::string> local_definition_json(const Document& doc, const ModuleAst& module,
                                                 const Json* params,
                                                 const std::string& query);

} // namespace dudu
