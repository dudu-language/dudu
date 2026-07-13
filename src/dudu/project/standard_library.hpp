#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace dudu {

std::filesystem::path standard_library_root();
std::map<std::string, std::filesystem::path>
with_standard_module_roots(std::map<std::string, std::filesystem::path> roots);

} // namespace dudu
