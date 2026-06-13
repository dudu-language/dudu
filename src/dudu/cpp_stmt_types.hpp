#pragma once

#include <map>
#include <string>

namespace dudu {

std::string infer_emitted_local_type(const std::string& expr,
                                     const std::map<std::string, std::string>& locals,
                                     const std::map<std::string, std::string>& function_returns);

} // namespace dudu
