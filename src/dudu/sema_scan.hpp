#pragma once

#include <cstddef>
#include <string>

namespace dudu {

size_t compound_assign_pos(const std::string& text, size_t assign);
size_t find_call_open(const std::string& expr);
size_t find_call_close(const std::string& expr, size_t open);
size_t find_top_level_comparison(const std::string& expr);
size_t find_top_level_operator(const std::string& expr);
std::string top_level_operator_text(const std::string& expr, size_t pos);
bool is_plain_identifier(const std::string& expr);

} // namespace dudu
