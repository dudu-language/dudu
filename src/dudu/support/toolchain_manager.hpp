#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dudu {

int run_toolchain_manager(const std::filesystem::path& executable,
                          const std::string& operation,
                          const std::vector<std::string>& arguments);

} // namespace dudu
