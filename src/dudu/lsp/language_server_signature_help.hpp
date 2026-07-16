#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <string>

namespace dudu {

struct Json;

std::string signature_help_json(const Document* doc, const Json* params);

} // namespace dudu
