#pragma once

#include <map>
#include <string>

namespace dudu {

std::string rewrite_pointer_members(std::string expr,
                                    const std::map<std::string, std::string>& locals);

} // namespace dudu
