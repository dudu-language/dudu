#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <string>

namespace dudu {

struct Json;

std::string completion_json(const Document* doc, const Json* params);
std::string completion_resolve_json(const Json* params);
std::string signature_help_json(const Document* doc, const Json* params);

} // namespace dudu
