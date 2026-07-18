#include "dudu/native/native_header_cache.hpp"

#include "dudu/core/file_io.hpp"
#include "dudu/native/native_header_cache_deps.hpp"
#include "dudu/project/project_config.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dudu {
namespace {

std::string read_text(const std::filesystem::path& path) {
    return try_read_text_file(path).value_or("");
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (out) {
        out << text;
    }
}

std::filesystem::path default_cache_dir(const NativeHeaderOptions& options) {
    if (!options.config.build_dir.empty()) {
        return project_path(options.config, options.config.build_dir) / "dudu-header-cache";
    }
    return std::filesystem::temp_directory_path() / "dudu-header-cache";
}

std::string cache_id(const std::string& key) {
    std::uint64_t first = 14695981039346656037ULL;
    std::uint64_t second = 1099511628211ULL;
    for (const unsigned char byte : key) {
        first = (first ^ byte) * 1099511628211ULL;
        second = (second + byte + 0x9e3779b97f4a7c15ULL) * 14029467366897019727ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << first << std::setw(16) << second;
    return out.str();
}

} // namespace

std::filesystem::path native_header_cache_dir(const NativeHeaderOptions& options) {
    return default_cache_dir(options);
}

std::filesystem::path clean_native_header_cache(const NativeHeaderOptions& options) {
    const std::filesystem::path dir = native_header_cache_dir(options);
    std::filesystem::remove_all(dir);
    return dir;
}

NativeHeaderRawCache load_native_header_raw_cache(const NativeHeaderOptions& options,
                                                  const std::string& key) {
    NativeHeaderRawCache cache;
    cache.base = default_cache_dir(options) / cache_id(key);
    const std::filesystem::path ast = cache.base.string() + ".ast";
    const std::filesystem::path macros = cache.base.string() + ".macros";
    const std::filesystem::path identities = cache.base.string() + ".identities";
    const std::filesystem::path deps = cache.base.string() + ".deps";
    if (!std::filesystem::exists(ast) || !std::filesystem::exists(macros) ||
        !std::filesystem::exists(identities) || !std::filesystem::exists(deps)) {
        return cache;
    }
    cache.dependencies = read_text(deps);
    if (!native_header_dependency_stamps_current(cache.dependencies)) {
        return cache;
    }
    cache.hit = true;
    return cache;
}

bool load_native_header_raw_cache_payload(NativeHeaderRawCache& cache) {
    const std::filesystem::path ast = cache.base.string() + ".ast";
    const std::filesystem::path macros = cache.base.string() + ".macros";
    const std::filesystem::path identities = cache.base.string() + ".identities";
    if (!std::filesystem::exists(ast) || !std::filesystem::exists(macros) ||
        !std::filesystem::exists(identities)) {
        cache.hit = false;
        cache.ast_dump.clear();
        cache.macro_dump.clear();
        cache.identity_dump.clear();
        return false;
    }
    cache.ast_dump = read_text(ast);
    cache.macro_dump = read_text(macros);
    cache.identity_dump = read_text(identities);
    cache.hit = !cache.ast_dump.empty() || !cache.macro_dump.empty();
    return cache.hit;
}

void store_native_header_raw_cache(const NativeHeaderRawCache& cache, const std::string& ast_dump,
                                   const std::string& macro_dump, const std::string& identity_dump,
                                   const std::string& dependencies,
                                   const std::filesystem::path& generated_source) {
    std::filesystem::create_directories(cache.base.parent_path());
    write_text(cache.base.string() + ".ast", ast_dump);
    write_text(cache.base.string() + ".macros", macro_dump);
    write_text(cache.base.string() + ".identities", identity_dump);
    write_text(cache.base.string() + ".deps",
               native_header_dependency_stamps_from_makefile(dependencies, generated_source));
}

} // namespace dudu
