#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <vector>

namespace dudu {

struct FormatPathOptions {
    std::vector<std::filesystem::path> excluded_dirs;
};

bool check_formatted_path(const std::filesystem::path& path,
                          const FormatPathOptions& options = {});
void format_path_in_place(const std::filesystem::path& path,
                          const FormatPathOptions& options = {});
void format_path(const std::filesystem::path& path,
                 const std::optional<std::filesystem::path>& output, std::ostream& stream,
                 const FormatPathOptions& options = {});

} // namespace dudu
