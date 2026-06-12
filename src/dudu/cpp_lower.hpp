#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::string lower_cpp_expr(std::string expr);
std::string lower_cpp_expr(std::string expr, const std::vector<std::string>& namespace_aliases);
std::string lower_cpp_type(const std::string& raw_type);
std::string replace_dots(std::string text);
std::string trim_copy(std::string text);
bool starts_with(std::string_view text, std::string_view prefix);
bool ends_with(std::string_view text, std::string_view suffix);
std::vector<std::string> split_top_level_args(const std::string& args);

} // namespace dudu
