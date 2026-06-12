#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>

namespace dudu {

bool check_formatted_path(const std::filesystem::path& path);
void format_path(const std::filesystem::path& path,
                 const std::optional<std::filesystem::path>& output, std::ostream& stream);

} // namespace dudu
