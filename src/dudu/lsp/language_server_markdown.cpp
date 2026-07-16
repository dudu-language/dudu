#include "dudu/lsp/language_server_markdown.hpp"

#include "dudu/lsp/language_server_json.hpp"

namespace dudu {

std::string fenced_code(std::string_view language, std::string_view code) {
    return "```" + std::string(language) + "\n" + std::string(code) + "\n```";
}

std::string markdown_hover_json(std::string_view markdown) {
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) + "\"}}";
}

} // namespace dudu
