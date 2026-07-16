#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace dudu {

std::map<std::string, Document> workspace_documents(
    const std::map<std::string, Document>& open_documents,
    const std::vector<std::filesystem::path>& workspace_roots);

} // namespace dudu
