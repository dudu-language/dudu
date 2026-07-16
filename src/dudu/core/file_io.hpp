#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dudu {

std::optional<std::string> try_read_text_file(const std::filesystem::path& path);
std::string read_required_text_file(const std::filesystem::path& path);
void write_required_text_file(const std::filesystem::path& path, std::string_view text);
std::string file_time_stamp(std::filesystem::file_time_type time);

} // namespace dudu
