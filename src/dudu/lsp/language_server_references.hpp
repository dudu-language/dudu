#pragma once

#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <map>
#include <string>
#include <vector>

namespace dudu {

struct Json;

std::vector<ReferenceLocation> reference_locations(const Document& doc, const Json* params,
                                                   const std::map<std::string, Document>& workspace,
                                                   bool include_native);
std::string references_json(const Document& doc, const Json* params,
                            const std::map<std::string, Document>& workspace);
std::string rename_json(const Document& doc, const Json* params,
                        const std::map<std::string, Document>& workspace);
std::string prepare_rename_json(const Document& doc, const Json* params);

} // namespace dudu
