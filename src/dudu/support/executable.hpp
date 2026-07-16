#pragma once

#include <filesystem>
#include <optional>

namespace dudu {

std::optional<std::filesystem::path> current_executable_path();
std::optional<std::filesystem::path> find_executable(const std::filesystem::path& command);

} // namespace dudu
