#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace dudu::project_index_internal {

std::filesystem::path canonical_key_path(const std::filesystem::path& path);
std::string path_key(const std::filesystem::path& path);
std::optional<std::filesystem::file_time_type> file_mtime(const std::filesystem::path& path);
std::string mtime_stamp(std::optional<std::filesystem::file_time_type> mtime);

} // namespace dudu::project_index_internal
