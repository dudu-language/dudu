#include "dudu/support/executable.hpp"

#include <array>
#include <cstdlib>
#include <string_view>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace dudu {
namespace {

std::optional<std::filesystem::path> regular_file(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
        return std::nullopt;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? std::optional<std::filesystem::path>{path}
                 : std::optional<std::filesystem::path>{canonical};
}

} // namespace

std::optional<std::filesystem::path> current_executable_path() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return std::nullopt;
    buffer.resize(buffer.find('\0'));
    return regular_file(buffer);
#elif defined(__linux__)
    std::array<char, 4096> buffer{};
    const ssize_t size = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (size <= 0 || static_cast<std::size_t>(size) == buffer.size())
        return std::nullopt;
    return regular_file(
        std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(size))));
#else
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> find_executable(const std::filesystem::path& command) {
    if (command.empty())
        return std::nullopt;
    if (command.has_parent_path())
        return regular_file(command);

    const char* path_value = std::getenv("PATH");
    if (path_value == nullptr)
        return std::nullopt;
    const std::string_view path(path_value);
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
        const std::string_view directory =
            path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
        const std::filesystem::path candidate =
            (directory.empty() ? std::filesystem::current_path()
                               : std::filesystem::path(directory)) /
            command;
        if (const std::optional<std::filesystem::path> found = regular_file(candidate))
            return found;
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return std::nullopt;
}

} // namespace dudu
