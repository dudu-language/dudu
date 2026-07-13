#include "dudu/project/standard_library.hpp"

#include <array>
#include <cstdlib>
#include <optional>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace dudu {
namespace {

std::optional<std::filesystem::path> executable_path() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return std::nullopt;
    }
    buffer.resize(buffer.find('\0'));
    return std::filesystem::path(buffer);
#elif defined(__linux__)
    std::array<char, 4096> buffer{};
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (size <= 0 || static_cast<std::size_t>(size) == buffer.size()) {
        return std::nullopt;
    }
    return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(size)));
#else
    return std::nullopt;
#endif
}

std::filesystem::path canonical_if_present(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        return {};
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

} // namespace

std::filesystem::path standard_library_root() {
    if (const char* configured = std::getenv("DUDU_LIBRARY_PATH")) {
        if (const std::filesystem::path root = canonical_if_present(configured); !root.empty()) {
            return root;
        }
    }
    if (const std::optional<std::filesystem::path> executable = executable_path()) {
        const std::filesystem::path bin = executable->parent_path();
        for (const std::filesystem::path& candidate :
             {bin / ".." / "share" / "dudu" / "lib", bin / ".." / "lib"}) {
            if (const std::filesystem::path root = canonical_if_present(candidate); !root.empty()) {
                return root;
            }
        }
    }
#ifdef DUDU_SOURCE_LIBRARY_DIR
    if (const std::filesystem::path root = canonical_if_present(DUDU_SOURCE_LIBRARY_DIR);
        !root.empty()) {
        return root;
    }
#endif
    return {};
}

std::map<std::string, std::filesystem::path>
with_standard_module_roots(std::map<std::string, std::filesystem::path> roots) {
    if (!roots.contains("dudu")) {
        if (const std::filesystem::path root = standard_library_root(); !root.empty()) {
            roots["dudu"] = root;
        }
    }
    return roots;
}

} // namespace dudu
