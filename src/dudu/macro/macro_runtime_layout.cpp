#include "dudu/macro/macro_runtime_layout.hpp"

#include "dudu/project/project_config.hpp"

#include <array>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace dudu::macro {
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
    const ssize_t size = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (size <= 0 || static_cast<std::size_t>(size) == buffer.size()) {
        return std::nullopt;
    }
    return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(size)));
#else
    return std::nullopt;
#endif
}

std::filesystem::path regular_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) ? path : std::filesystem::path{};
}

std::filesystem::path header_root(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path / "dudu/macro/macro_worker_runtime.hpp", error)
               ? path
               : std::filesystem::path{};
}

std::vector<std::filesystem::path> resolve_paths(const ProjectConfig& config,
                                                 const std::vector<std::string>& paths) {
    std::vector<std::filesystem::path> out;
    out.reserve(paths.size());
    for (const std::string& path : paths) {
        out.push_back(project_path(config, path));
    }
    return out;
}

std::string compiler_identity(const ProjectConfig& config) {
    const std::string compiler = config.compiler.empty() ? "c++" : config.compiler;
    std::error_code error;
    const auto timestamp = std::filesystem::last_write_time(compiler, error);
    return compiler + ":" + (error ? std::string("path")
                                   : std::to_string(timestamp.time_since_epoch().count()));
}

} // namespace

RuntimeLayout find_runtime_layout() {
    if (const char* include = std::getenv("DUDU_MACRO_RUNTIME_INCLUDE")) {
        const char* library = std::getenv("DUDU_MACRO_RUNTIME_LIBRARY");
        RuntimeLayout layout{.include_dirs = {header_root(include)},
                             .library = library == nullptr ? std::filesystem::path{}
                                                          : regular_file(library)};
        if (!layout.include_dirs.front().empty() && !layout.library.empty()) {
            return layout;
        }
        throw std::runtime_error("invalid DUDU macro runtime environment paths");
    }
    if (const auto executable = executable_path()) {
        const std::filesystem::path bin = executable->parent_path();
        const std::array<std::pair<std::filesystem::path, std::filesystem::path>, 4> candidates = {
            std::pair{bin / "../src", bin / "libdudu_macro_runtime.a"},
            std::pair{bin / "../include", bin / "../lib/libdudu_macro_runtime.a"},
            std::pair{bin / "../include", bin / "../lib64/libdudu_macro_runtime.a"},
            std::pair{bin / "../include", bin / "../lib/dudu/libdudu_macro_runtime.a"}};
        for (const auto& [include, library] : candidates) {
            const std::filesystem::path resolved_include = header_root(include.lexically_normal());
            const std::filesystem::path resolved_library = regular_file(library.lexically_normal());
            if (!resolved_include.empty() && !resolved_library.empty()) {
                return {.include_dirs = {resolved_include}, .library = resolved_library};
            }
        }
    }
    throw std::runtime_error(
        "Dudu macro runtime was not found beside the compiler; reinstall Dudu or set "
        "DUDU_MACRO_RUNTIME_INCLUDE and DUDU_MACRO_RUNTIME_LIBRARY");
}

WorkerBuildOptions worker_build_options(const ProjectConfig& config,
                                        const RuntimeLayout& runtime,
                                        const std::filesystem::path& cache_dir,
                                        std::string package,
                                        std::vector<std::string> capabilities) {
    WorkerBuildOptions options;
    options.cache_dir = cache_dir;
    options.package = std::move(package);
    options.compiler = config.compiler.empty() ? "c++" : config.compiler;
    options.cpp_standard = config.cpp_std;
    options.toolchain_identity = compiler_identity(config);
    options.runtime_include_dirs = runtime.include_dirs;
    options.runtime_library = runtime.library;
    options.include_dirs = resolve_paths(config, config.include_dirs);
    options.library_dirs = resolve_paths(config, config.lib_dirs);
    options.cpp_sources = resolve_paths(config, config.cpp_sources);
    options.defines = config.defines;
    options.compiler_flags = config.flags;
    options.libraries = config.libs;
    options.linker_flags = config.link_flags;
    options.capabilities = std::move(capabilities);
    options.non_cacheable_macros = config.non_cacheable_macros;
    return options;
}

} // namespace dudu::macro
