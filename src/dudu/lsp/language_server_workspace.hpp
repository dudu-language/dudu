#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <map>
#include <string>

namespace dudu {

std::map<std::string, Document>
workspace_documents(const std::map<std::string, Document>& open_documents);

} // namespace dudu
