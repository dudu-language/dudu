#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dudu {

size_t raw_escape_find_matching(std::string_view text, size_t open, char left, char right);
std::string lower_dotted_template_call(std::string expr,
                                       const std::vector<std::string>& namespace_aliases);
std::string lower_offsetof_call(std::string expr);
std::string lower_template_alloc_call(std::string expr, std::string_view name);
std::string lower_template_value_call(std::string expr, std::string_view name);
std::string lower_type_operator_call(std::string expr, std::string_view name);

} // namespace dudu
