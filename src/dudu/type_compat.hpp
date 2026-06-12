#pragma once

#include <string>

namespace dudu {

bool assignment_type_allowed(const std::string& expected, const std::string& expr,
                             const std::string& got);

} // namespace dudu
