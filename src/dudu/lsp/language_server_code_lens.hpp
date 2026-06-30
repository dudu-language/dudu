#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <map>
#include <string>

namespace dudu {

struct Json;

std::string code_lens_json(const Document& doc, const Json* params,
                           const std::map<std::string, Document>& workspace);

} // namespace dudu
