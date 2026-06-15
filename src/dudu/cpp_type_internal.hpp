#pragma once

#include <string>
#include <vector>

namespace dudu {

std::string strip_c_import_type_aliases(std::string type,
                                        const std::vector<std::string>& namespace_aliases);

} // namespace dudu
