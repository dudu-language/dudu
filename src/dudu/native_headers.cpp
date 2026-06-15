#include "dudu/native_headers.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_header_cache.hpp"
#include "dudu/native_header_merge.hpp"
#include "dudu/native_header_parse.hpp"
#include "dudu/native_header_scope.hpp"
#include "dudu/native_header_types.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace dudu {
namespace {
bool is_foreign(const ImportDecl& import) {
    return import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp;
}
bool direct_import(const ImportDecl& import) {
    return import.alias.empty();
}
std::string unquoted(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}
std::filesystem::path absolute_from(const std::filesystem::path& base,
                                    const std::filesystem::path& path) {
    return path.is_absolute() ? path : base / path;
}
std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("could not write " + path.string());
    }
    out << text;
}
std::string capture_pkg_config_cflags(const std::vector<std::string>& packages) {
    if (packages.empty()) {
        return {};
    }
    const char* pkg_config = std::getenv("PKG_CONFIG");
    std::string command =
        shell_quote_arg(pkg_config == nullptr ? "pkg-config" : std::string(pkg_config)) +
        " --cflags";
    for (const std::string& package : packages) {
        command += " " + shell_quote_arg(package);
    }
    command += " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }
    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    if (pclose(pipe) != 0) {
        return {};
    }
    return output;
}
std::string scanner_flags(const NativeHeaderOptions& options) {
    std::string flags = " -I" + shell_quote_arg(options.source_dir.string());
    for (const std::string& include_dir : options.config.include_dirs) {
        flags +=
            " " + shell_quote_arg("-I" + absolute_from(options.source_dir, include_dir).string());
    }
    for (const std::string& define : options.config.defines) {
        flags += " " + shell_quote_arg("-D" + define);
    }
    for (const std::string& flag : options.config.flags) {
        flags += " " + shell_quote_arg(flag);
    }
    const std::string pkg_flags =
        trim_copy(capture_pkg_config_cflags(options.config.pkg_config_packages));
    return pkg_flags.empty() ? flags : flags + " " + pkg_flags;
}
std::filesystem::path temp_base(const std::filesystem::path& source_dir) {
    const auto ticks = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return source_dir / (".dudu_native_headers_" + ticks);
}
bool requires_scan_success(const ImportDecl& import) {
    const std::filesystem::path header = unquoted(import.module_path);
    const std::string text = header.string();
    return header.is_absolute() || starts_with(text, ".") || starts_with(text, "/");
}
std::string header_stamp(const ImportDecl& import, const NativeHeaderOptions& options) {
    const std::filesystem::path header = unquoted(import.module_path);
    const std::filesystem::path path =
        header.is_absolute() ? header : (options.source_dir / header).lexically_normal();
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return {};
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return {};
    }
    const auto mtime = std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    return "|" + path.string() + "|" + std::to_string(size) + "|" +
           std::to_string(mtime.time_since_epoch().count());
}
std::string run_capture(const std::string& command, const std::filesystem::path& output,
                        const std::filesystem::path& error) {
    const int status = std::system(
        (command + " >" + shell_quote_path(output) + " 2>" + shell_quote_path(error)).c_str());
    if (status != 0) {
        return {};
    }
    return read_text(output);
}
std::string clangxx_command() {
    const char* clang_env = std::getenv("CLANGXX");
    return clang_env == nullptr ? "clang++" : std::string(clang_env);
}
std::string clang_base_command(const NativeHeaderOptions& options, const std::filesystem::path& cpp,
                               bool ast_dump) {
    const std::string clang = clangxx_command();
    std::string command = shell_quote_arg(clang) +
                          " -std=" + shell_quote_arg(options.config.cpp_std) +
                          " -x c++ -fsyntax-only -fno-color-diagnostics ";
    if (ast_dump) {
        command += "-Xclang -ast-dump ";
    }
    return command + shell_quote_path(cpp) + scanner_flags(options);
}
std::string scan_error_message(const ImportDecl& import, std::string detail,
                               const std::string& clang) {
    std::ostringstream out;
    out << "could not scan native header " << unquoted(import.module_path);
    detail = trim_copy(std::move(detail));
    if (!detail.empty()) {
        out << "\n" << detail;
    }
    if (!clang.empty() && detail.find(clang) != std::string::npos) {
        out << "\nhint: native header awareness requires clang++; install clang or set CLANGXX";
    } else if (detail.find("not found") != std::string::npos ||
               detail.find("No such file") != std::string::npos) {
        out << "\nhint: add the header directory to [include].paths or the package to [pkg].libs";
    } else {
        out << "\nhint: native header awareness requires clang++; install clang or set CLANGXX";
    }
    return out.str();
}
bool public_direct_macro_name(const std::string& name) {
    return !name.empty() &&
           (std::isupper(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_');
}
std::string scan_key(const ImportDecl& import, const NativeHeaderOptions& options,
                     const std::string& flags) {
    return unquoted(import.module_path) + "|" + clangxx_command() + "|" + options.config.cpp_std +
           "|" + flags + header_stamp(import, options);
}
NativeHeaderScan scan_one_header(const ImportDecl& import, const NativeHeaderOptions& options,
                                 const std::string& flags) {
    static std::map<std::string, NativeHeaderScan> cache;
    const std::string key = scan_key(import, options, flags);
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }
    NativeHeaderRawCache raw_cache = load_native_header_raw_cache(options, key);
    if (raw_cache.hit) {
        NativeHeaderScan scan;
        parse_ast_dump(scan, raw_cache.ast_dump, import.location);
        parse_macro_dump(scan, raw_cache.macro_dump, import.location);
        cache[key] = dedupe_scan(std::move(scan));
        return cache[key];
    }
    const std::filesystem::path base = temp_base(options.source_dir);
    const std::filesystem::path cpp = base.string() + ".cpp";
    const std::filesystem::path ast = base.string() + ".ast";
    const std::filesystem::path macros = base.string() + ".macros";
    const std::filesystem::path err = base.string() + ".err";
    const auto cleanup = [&]() {
        std::filesystem::remove(cpp);
        std::filesystem::remove(ast);
        std::filesystem::remove(macros);
        std::filesystem::remove(err);
    };
    write_text(cpp, "#include \"" + unquoted(import.module_path) + "\"\nint dudu_probe = 0;\n");

    NativeHeaderScan scan;
    const std::string ast_dump = run_capture(clang_base_command(options, cpp, true), ast, err);
    if (!ast_dump.empty()) {
        parse_ast_dump(scan, ast_dump, import.location);
    }
    const std::string clang = clangxx_command();
    const std::string macro_cmd = shell_quote_arg(clang) +
                                  " -std=" + shell_quote_arg(options.config.cpp_std) +
                                  " -x c++ -dM -E " + shell_quote_path(cpp) + flags;
    const std::string macro_dump = run_capture(macro_cmd, macros, err);
    if (ast_dump.empty() && macro_dump.empty()) {
        if (!requires_scan_success(import)) {
            cleanup();
            return {};
        }
        const std::string detail = read_text(err);
        cleanup();
        throw CompileError(import.location, scan_error_message(import, detail, clangxx_command()));
    }
    parse_macro_dump(scan, macro_dump, import.location);
    store_native_header_raw_cache(raw_cache, ast_dump, macro_dump);
    cleanup();
    cache[key] = dedupe_scan(std::move(scan));
    return cache[key];
}
template <typename T> void append_unique(std::vector<T>& target, const std::vector<T>& source) {
    std::set<std::string> seen;
    for (const T& item : target)
        seen.insert(item.name);
    for (const T& item : source)
        if (seen.insert(item.name).second)
            target.push_back(item);
}
template <typename T>
std::vector<T> prefixed_names(const std::vector<T>& source, const std::string& prefix) {
    std::vector<T> out;
    out.reserve(source.size());
    for (T item : source) {
        item.name = prefix + "." + item.name;
        out.push_back(std::move(item));
    }
    return out;
}
std::vector<NativeTypeDecl> prefixed_type_names(const std::vector<NativeTypeDecl>& source,
                                                const std::string& prefix) {
    std::vector<NativeTypeDecl> out;
    out.reserve(source.size());
    for (NativeTypeDecl item : source) {
        const std::string original = item.name;
        item.name = prefix + "." + item.name;
        if (item.type.empty())
            item.type = original;
        out.push_back(std::move(item));
    }
    return out;
}
std::vector<NativeMacroDecl> direct_macros(const std::vector<NativeMacroDecl>& source) {
    std::vector<NativeMacroDecl> out;
    for (const NativeMacroDecl& item : source)
        if (public_direct_macro_name(item.name))
            out.push_back(item);
    return out;
}
} // namespace
NativeHeaderScan scan_native_headers(const ModuleAst& module, const NativeHeaderOptions& options) {
    const std::string flags = scanner_flags(options);
    NativeHeaderScan out;
    for (const ImportDecl& import : module.imports) {
        if (!is_foreign(import)) {
            continue;
        }
        NativeHeaderScan scan = scan_one_header(import, options, flags);
        if (direct_import(import)) {
            append_unique(out.types, scan.types);
            append_unique(out.classes, scan.classes);
            append_unique(out.values, scan.values);
            append_unique_native_functions(out.functions, scan.functions);
            append_unique(out.macros, direct_macros(scan.macros));
            append_unique(out.namespaces, scan.namespaces);
        } else {
            append_unique(out.types, scan.types);
            append_unique(out.classes, scan.classes);
            append_unique(out.types, prefixed_type_names(scan.types, import.alias));
            append_unique(out.classes, prefixed_names(scan.classes, import.alias));
            append_unique(out.values, prefixed_names(scan.values, import.alias));
            append_unique_native_functions(out.functions,
                                           prefixed_names(scan.functions, import.alias));
            append_unique(out.macros, prefixed_names(scan.macros, import.alias));
        }
    }
    return out;
}
void merge_native_headers(ModuleAst& module, const NativeHeaderOptions& options) {
    NativeHeaderScan scan = scan_native_headers(module, options);
    append_unique(module.native_types, scan.types);
    append_unique(module.native_values, scan.values);
    append_unique_native_functions(module.native_functions, scan.functions);
    append_unique(module.native_macros, scan.macros);
    append_unique(module.native_namespaces, scan.namespaces);
    append_unique(module.native_classes, scan.classes);
}
std::vector<NativeTypeDecl> scan_native_header_types(const ModuleAst& module,
                                                     const NativeHeaderOptions& options) {
    return scan_native_headers(module, options).types;
}

void merge_native_header_types(ModuleAst& module, const NativeHeaderOptions& options) {
    merge_native_headers(module, options);
}

} // namespace dudu
