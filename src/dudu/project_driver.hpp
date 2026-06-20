#pragma once

#include <filesystem>
#include <string>

namespace dudu {

std::filesystem::path clean_project(const std::filesystem::path& dir);
void init_project(const std::filesystem::path& dir);
void new_project(const std::filesystem::path& dir);
void set_project_step_timings(bool enabled);
void print_project_step(bool enabled, const std::string& label, const std::filesystem::path& path);

} // namespace dudu
