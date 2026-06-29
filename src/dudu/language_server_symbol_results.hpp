#pragma once

#include "dudu/language_server_types.hpp"
#include "dudu/project_index.hpp"

#include <map>
#include <string>

namespace dudu {

std::string document_symbols_json(const Document& doc);
std::string workspace_symbols_json(const std::string& query, const ProjectIndex& index,
                                   const Document& seed_doc);
std::string workspace_symbols_json(const std::string& query,
                                   const std::map<std::string, Document>& open_documents);

} // namespace dudu
