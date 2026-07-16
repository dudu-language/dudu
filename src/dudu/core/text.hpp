#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::string_view trim_view(std::string_view text);
std::string trim_string(std::string_view text);
bool is_identifier_continue(char c);
bool is_identifier_start(char c);
bool is_identifier(std::string_view text);
bool starts_with(std::string_view text, std::string_view prefix);
bool ends_with(std::string_view text, std::string_view suffix);
std::vector<std::string> split_top_level_args(std::string_view text);

} // namespace dudu
