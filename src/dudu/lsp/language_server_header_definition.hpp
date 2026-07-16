#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

struct Json;

std::optional<std::string>
native_header_definition_json(const Document& doc, const ModuleAst& current, const Json* params);

} // namespace dudu
