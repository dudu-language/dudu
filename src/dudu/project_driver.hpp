#pragma once

#include <filesystem>

namespace dudu {

void init_project(const std::filesystem::path& dir);
void new_project(const std::filesystem::path& dir);

} // namespace dudu
