#pragma once

#include <cstddef>
#include <string>

namespace dudu {

size_t find_call_open(const std::string& expr);
size_t find_call_close(const std::string& expr, size_t open);
bool is_plain_identifier(const std::string& expr);

} // namespace dudu
