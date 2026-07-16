#include "dudu/project/standard_library.hpp"

#include "dudu/support/executable.hpp"

#include <cstdlib>
#include <system_error>

namespace dudu {
namespace {

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
    if (const std::optional<std::filesystem::path> executable = current_executable_path()) {
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
