#include "dudu/native_header_cache.hpp"

#include <functional>
#include <fstream>

namespace dudu {
namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (out) {
        out << text;
    }
}

std::filesystem::path default_cache_dir(const NativeHeaderOptions& options) {
    if (!options.config.build_dir.empty()) {
        return options.config.build_dir / "dudu-header-cache";
    }
    return std::filesystem::temp_directory_path() / "dudu-header-cache";
}

std::string cache_id(const std::string& key) {
    return std::to_string(std::hash<std::string>{}(key));
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
    if (!std::filesystem::exists(ast) || !std::filesystem::exists(macros)) {
        return cache;
    }
    cache.ast_dump = read_text(ast);
    cache.macro_dump = read_text(macros);
    cache.hit = !cache.ast_dump.empty() || !cache.macro_dump.empty();
    return cache;
}

void store_native_header_raw_cache(const NativeHeaderRawCache& cache,
                                   const std::string& ast_dump,
                                   const std::string& macro_dump) {
    std::filesystem::create_directories(cache.base.parent_path());
    write_text(cache.base.string() + ".ast", ast_dump);
    write_text(cache.base.string() + ".macros", macro_dump);
}

} // namespace dudu
