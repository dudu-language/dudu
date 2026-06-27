#include "dudu/native_header_cache_deps.hpp"

#include <filesystem>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

namespace dudu {
namespace {

std::optional<std::string> file_stamp(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    const auto mtime = std::filesystem::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }
    return path.lexically_normal().string() + "\t" + std::to_string(size) + "\t" +
           std::to_string(mtime.time_since_epoch().count());
}

std::string unescape_make_path(std::string path) {
    std::string out;
    out.reserve(path.size());
    bool escaped = false;
    for (const char c : path) {
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        out.push_back(c);
    }
    if (escaped) {
        out.push_back('\\');
    }
    return out;
}

std::vector<std::filesystem::path> dependency_paths_from_makefile(std::string text) {
    for (size_t pos = 0; (pos = text.find("\\\n", pos)) != std::string::npos;) {
        text.replace(pos, 2, " ");
    }
    const size_t colon = text.find(':');
    if (colon == std::string::npos) {
        return {};
    }
    std::vector<std::filesystem::path> out;
    std::istringstream in(text.substr(colon + 1));
    std::string item;
    while (in >> item) {
        out.push_back(unescape_make_path(std::move(item)));
    }
    return out;
}

} // namespace

std::string
native_header_dependency_stamps_from_makefile(const std::string& make_deps,
                                              const std::filesystem::path& generated_source) {
    std::ostringstream out;
    const std::filesystem::path ignored =
        generated_source.empty() ? std::filesystem::path{} : generated_source.lexically_normal();
    for (const std::filesystem::path& path : dependency_paths_from_makefile(make_deps)) {
        if (!ignored.empty() && path.lexically_normal() == ignored) {
            continue;
        }
        if (const std::optional<std::string> stamp = file_stamp(path)) {
            out << *stamp << '\n';
        }
    }
    return out.str();
}

bool native_header_dependency_stamps_current(const std::string& stamps) {
    if (stamps.empty()) {
        return false;
    }
    std::istringstream in(stamps);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const size_t first = line.find('\t');
        const size_t second =
            first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            return false;
        }
        const std::filesystem::path path = line.substr(0, first);
        const std::optional<std::string> stamp = file_stamp(path);
        if (!stamp || *stamp != line) {
            return false;
        }
    }
    return true;
}

} // namespace dudu
