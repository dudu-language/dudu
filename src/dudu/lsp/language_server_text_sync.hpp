#pragma once

#include "dudu/lsp/language_server_json.hpp"

#include <string>

namespace dudu {

void apply_lsp_content_changes(std::string& text, const JsonArray& changes);

} // namespace dudu
