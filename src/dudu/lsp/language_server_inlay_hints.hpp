#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <string>

namespace dudu {

struct Json;

std::string inlay_hints_json(const Document& doc, const Json* params);

} // namespace dudu
