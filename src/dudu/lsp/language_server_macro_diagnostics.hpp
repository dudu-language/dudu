#pragma once

#include "dudu/lsp/language_server_types.hpp"
#include "dudu/macro/macro_expansion.hpp"

#include <vector>

namespace dudu {

std::vector<Diagnostic> macro_diagnostics_for_document(const macro::ExpansionReport& report,
                                                       const Document& document);

} // namespace dudu
