#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

struct Json;

std::optional<std::string> keyword_hover_json(const Document& doc, const Json* params);

} // namespace dudu
