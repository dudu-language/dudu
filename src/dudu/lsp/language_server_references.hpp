#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <map>
#include <string>

namespace dudu {

struct Json;

std::string references_json(const Document& doc, const Json* params,
                            const std::map<std::string, Document>& workspace);
std::string rename_json(const Document& doc, const Json* params,
                        const std::map<std::string, Document>& workspace);
std::string prepare_rename_json(const Document& doc, const Json* params);

} // namespace dudu
