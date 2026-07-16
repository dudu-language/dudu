#pragma once

#include <string>
#include <string_view>

namespace dudu {

std::string fenced_code(std::string_view language, std::string_view code);
std::string markdown_hover_json(std::string_view markdown);

} // namespace dudu
