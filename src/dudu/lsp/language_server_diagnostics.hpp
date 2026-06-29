#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <string>
#include <vector>

namespace dudu {

std::vector<Diagnostic> diagnostics_for_document(const Document& doc);
std::string diagnostic_json(const Diagnostic& diagnostic);

} // namespace dudu
