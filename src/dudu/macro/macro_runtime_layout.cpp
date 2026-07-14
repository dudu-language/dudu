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

std::filesystem::path resolve_executable(std::string_view command) {
    const std::filesystem::path direct(command);
    if (direct.has_parent_path())
        return direct;
    const char* path_value = std::getenv("PATH");
    if (path_value == nullptr)
        return direct;
    std::string_view path(path_value);
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
        const std::filesystem::path candidate =
            std::filesystem::path(path.substr(start, end - start)) / direct;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return candidate;
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return direct;
}

std::string compiler_identity(const ProjectConfig& config) {
    const std::string compiler = config.compiler.empty() ? "c++" : config.compiler;
    const std::filesystem::path resolved = resolve_executable(compiler);
    std::error_code error;
    const auto timestamp = std::filesystem::last_write_time(resolved, error);
    if (error)
        return compiler + ":unresolved";
    const std::uintmax_t size = std::filesystem::file_size(resolved, error);
    return resolved.lexically_normal().string() + ":" +
           std::to_string(timestamp.time_since_epoch().count()) + ":" +
           (error ? std::string("unknown-size") : std::to_string(size));
}

std::string file_identity(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
    const std::filesystem::path effective = error ? path : resolved;
    const auto timestamp = std::filesystem::last_write_time(effective, error);
    if (error)
        return effective.lexically_normal().string() + ":unresolved";
    const std::uintmax_t size = std::filesystem::file_size(effective, error);
    return effective.lexically_normal().string() + ":" +
           std::to_string(timestamp.time_since_epoch().count()) + ":" +
           (error ? std::string("unknown-size") : std::to_string(size));
}

std::filesystem::path default_macro_sdk_cache(const std::filesystem::path& worker_cache) {
    if (const char* configured = std::getenv("DUDU_MACRO_SDK_CACHE")) {
        return configured;
    }
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        return std::filesystem::path(xdg) / "dudu/macro-sdk";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".cache/dudu/macro-sdk";
    }
    return worker_cache.parent_path() / "sdk";
}

} // namespace

RuntimeLayout find_runtime_layout() {
    if (const char* include = std::getenv("DUDU_MACRO_RUNTIME_INCLUDE")) {
        const char* library = std::getenv("DUDU_MACRO_RUNTIME_LIBRARY");
        const char* bridge = std::getenv("DUDU_MACRO_SDK_BRIDGE_SOURCE");
        RuntimeLayout layout{
            .include_dirs = {header_root(include)},
            .library = library == nullptr ? std::filesystem::path{} : regular_file(library),
            .sdk_bridge_source = bridge == nullptr ? regular_file(std::filesystem::path(include) /
                                                                  "dudu/macro/"
                                                                  "macro_sdk_bridge_generated.cpp")
                                                   : regular_file(bridge)};
        if (!layout.include_dirs.front().empty() && !layout.library.empty() &&
            !layout.sdk_bridge_source.empty()) {
            return layout;
        }
        throw std::runtime_error("invalid DUDU macro runtime environment paths");
    }
    if (const auto executable = executable_path()) {
        const std::filesystem::path bin = executable->parent_path();
        struct Candidate {
            std::filesystem::path include;
            std::filesystem::path library;
            std::filesystem::path bridge;
        };
        const std::array<Candidate, 4> candidates = {
            Candidate{bin / "../src", bin / "libdudu_macro_runtime.a",
                      bin / "../src/dudu/macro/macro_sdk_bridge_generated.cpp"},
            Candidate{bin / "../include", bin / "../lib/libdudu_macro_runtime.a",
                      bin / "../share/dudu/macro/macro_sdk_bridge_generated.cpp"},
            Candidate{bin / "../include", bin / "../lib64/libdudu_macro_runtime.a",
                      bin / "../share/dudu/macro/macro_sdk_bridge_generated.cpp"},
            Candidate{bin / "../include", bin / "../lib/dudu/libdudu_macro_runtime.a",
                      bin / "../share/dudu/macro/macro_sdk_bridge_generated.cpp"}};
        for (const auto& [include, library, bridge] : candidates) {
            const std::filesystem::path resolved_include = header_root(include.lexically_normal());
            const std::filesystem::path resolved_library = regular_file(library.lexically_normal());
            const std::filesystem::path resolved_bridge = regular_file(bridge.lexically_normal());
            if (!resolved_include.empty() && !resolved_library.empty() &&
                !resolved_bridge.empty()) {
                return {.include_dirs = {resolved_include},
                        .library = resolved_library,
                        .sdk_bridge_source = resolved_bridge};
            }
        }
    }
    throw std::runtime_error(
        "Dudu macro runtime was not found beside the compiler; reinstall Dudu or set "
        "DUDU_MACRO_RUNTIME_INCLUDE and DUDU_MACRO_RUNTIME_LIBRARY");
}

WorkerBuildOptions worker_build_options(const ProjectConfig& config, const RuntimeLayout& runtime,
                                        const std::filesystem::path& cache_dir, std::string package,
                                        std::vector<std::string> capabilities) {
    WorkerBuildOptions options;
    options.cache_dir = cache_dir;
    options.sdk_cache_dir = default_macro_sdk_cache(cache_dir);
    options.project_root = config.project_dir;
    options.package = std::move(package);
    options.compiler = config.compiler.empty() ? "c++" : config.compiler;
    options.cpp_standard = config.cpp_std;
    options.toolchain_identity = compiler_identity(config);
    const std::optional<std::filesystem::path> dudu_executable = executable_path();
    options.dudu_toolchain_identity =
        dudu_executable ? file_identity(*dudu_executable) : "dudu:unresolved";
    options.runtime_include_dirs = runtime.include_dirs;
    options.runtime_library = runtime.library;
    options.sdk_bridge_source = runtime.sdk_bridge_source;
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
