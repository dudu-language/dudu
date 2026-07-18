#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <map>
#include <string>
#include <string_view>

namespace dudu {

struct StructuredDocumentation {
    std::string body;
    std::map<std::string, std::string> parameters;
    std::map<std::string, std::string> template_parameters;
    std::string returns;
    std::string deprecated;
};

StructuredDocumentation parse_documentation(std::string_view text);
std::string documentation_markdown(std::string_view text);
std::string parameter_documentation(std::string_view text, std::string_view parameter);
std::string symbol_documentation_markdown(const Symbol& symbol);

} // namespace dudu
