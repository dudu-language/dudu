#pragma once

#include <filesystem>

namespace dudu {

std::filesystem::path clean_project(const std::filesystem::path& dir);
void init_project(const std::filesystem::path& dir);
void new_project(const std::filesystem::path& dir);

} // namespace dudu
