#include "dudu/project/project_index_internal.hpp"

#include "dudu/core/file_io.hpp"

#include <system_error>

namespace dudu::project_index_internal {

std::filesystem::path canonical_key_path(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

std::string path_key(const std::filesystem::path& path) {
    return canonical_key_path(path).string();
}

std::optional<std::filesystem::file_time_type> file_mtime(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(path, error);
    return error ? std::nullopt : std::optional<std::filesystem::file_time_type>{mtime};
}

std::string mtime_stamp(std::optional<std::filesystem::file_time_type> mtime) {
    return mtime.has_value() ? file_time_stamp(*mtime) : std::string{};
}

} // namespace dudu::project_index_internal
