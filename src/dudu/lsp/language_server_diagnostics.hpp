#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace dudu {

std::vector<Diagnostic> diagnostics_for_document(const Document& doc);
std::vector<Diagnostic> syntax_diagnostics_for_document(const Document& doc);
std::vector<Diagnostic> diagnostics_for_document_snapshot(
    const Document& doc, const std::map<std::filesystem::path, std::string>& source_overrides);
std::string diagnostic_json(const Diagnostic& diagnostic);

} // namespace dudu
