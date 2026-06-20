#include "dudu/native_headers.hpp"

#include "dudu/ast_type.hpp"
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
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
namespace dudu {
namespace {
bool is_foreign(const ImportDecl& import) {
    return import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCxx ||
           import.kind == ImportKind::ForeignCpp;
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
std::string unquoted_source_file(std::string value) {
    value = trim_copy(std::move(value));
    while (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                 (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}
void append_include_flag(std::string& flags, const std::filesystem::path& path) {
    flags += " " + shell_quote_arg("-I" + path.lexically_normal().string());
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
    std::string flags;
    append_include_flag(flags, options.source_dir);
    for (const std::string& include_dir : options.config.include_dirs) {
        append_include_flag(flags, project_path(options.config, include_dir));
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
std::string include_line(const std::string& header) {
    return "#include \"" + header + "\"\n";
}
std::string scanner_source_for_header(const ImportDecl& import, bool with_c_prelude) {
    std::string source;
    if (with_c_prelude) {
        source += "#include <stddef.h>\n#include <stdio.h>\n";
    }
    source += include_line(unquoted(import.module_path));
    source += "int dudu_probe = 0;\n";
    return source;
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
std::optional<std::filesystem::path>
normalized_existing_header_path(const ImportDecl& import, const NativeHeaderOptions& options) {
    const std::filesystem::path header = unquoted(import.module_path);
    const std::filesystem::path path =
        header.is_absolute() ? header : (options.source_dir / header).lexically_normal();
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return std::nullopt;
    }
    return path.lexically_normal();
}
bool source_file_matches_header(const SourceLocation& location, const ImportDecl& import,
                                const NativeHeaderOptions& options) {
    std::string file = unquoted_source_file(location.file);
    if (file.empty()) {
        return false;
    }
    const std::filesystem::path source_path = std::filesystem::path(file).lexically_normal();
    if (const auto header_path = normalized_existing_header_path(import, options)) {
        return source_path == *header_path;
    }
    const std::filesystem::path imported = unquoted(import.module_path);
    if (imported.has_parent_path()) {
        return source_path.string().ends_with(imported.lexically_normal().string());
    }
    return source_path.filename() == imported.filename();
}

bool source_file_belongs_to_import_family(const SourceLocation& location, const ImportDecl& import,
                                          const NativeHeaderOptions& options) {
    if (source_file_matches_header(location, import, options)) {
        return true;
    }
    const std::filesystem::path imported =
        std::filesystem::path(unquoted(import.module_path)).lexically_normal();
    if (!imported.has_parent_path()) {
        return false;
    }
    std::string file = unquoted_source_file(location.file);
    if (file.empty()) {
        return false;
    }
    const std::filesystem::path source_path = std::filesystem::path(file).lexically_normal();
    const std::filesystem::path parent = imported.parent_path();
    std::string source_text = source_path.string();
    std::string parent_text = parent.string();
    if (!source_text.empty() && source_text.front() != '/') {
        source_text = "/" + source_text;
    }
    if (!parent_text.empty() && parent_text.front() != '/') {
        parent_text = "/" + parent_text;
    }
    return !parent_text.empty() && source_text.find(parent_text + "/") != std::string::npos;
}

std::vector<NativeFunctionDecl> alias_visible_functions(const NativeHeaderScan& scan,
                                                        const ImportDecl& import,
                                                        const NativeHeaderOptions& options) {
    if (import.kind == ImportKind::ForeignCpp) {
        return scan.functions;
    }
    std::vector<NativeFunctionDecl> out;
    for (const NativeFunctionDecl& function : scan.functions) {
        const bool macro_stub = function.location.file == import.location.file &&
                                function.location.line == import.location.line;
        if (macro_stub ||
            source_file_belongs_to_import_family(function.location, import, options)) {
            out.push_back(function);
        }
    }
    return out;
}

template <typename T>
std::vector<T> filter_native_decls_to_header(const std::vector<T>& source, const ImportDecl& import,
                                             const NativeHeaderOptions& options) {
    std::vector<T> out;
    for (const T& item : source) {
        if (source_file_matches_header(item.location, import, options)) {
            out.push_back(item);
        }
    }
    return out;
}
NativeHeaderScan filter_ast_scan_to_header(NativeHeaderScan scan, const ImportDecl& import,
                                           const NativeHeaderOptions& options) {
    scan.types = filter_native_decls_to_header(scan.types, import, options);
    scan.values = filter_native_decls_to_header(scan.values, import, options);
    scan.functions = filter_native_decls_to_header(scan.functions, import, options);
    scan.namespaces = filter_native_decls_to_header(scan.namespaces, import, options);
    scan.classes = filter_native_decls_to_header(scan.classes, import, options);
    return scan;
}
std::string scan_key(const ImportDecl& import, const NativeHeaderOptions& options,
                     const std::string& flags) {
    return "v3|" + unquoted(import.module_path) + "|" + clangxx_command() + "|" +
           options.config.cpp_std + "|" + flags + header_stamp(import, options);
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
    write_text(cpp, scanner_source_for_header(import, false));

    NativeHeaderScan scan;
    std::string ast_dump = run_capture(clang_base_command(options, cpp, true), ast, err);
    bool used_prelude_retry = false;
    if (ast_dump.empty()) {
        write_text(cpp, scanner_source_for_header(import, true));
        ast_dump = run_capture(clang_base_command(options, cpp, true), ast, err);
        used_prelude_retry = !ast_dump.empty();
    }
    if (!ast_dump.empty()) {
        NativeHeaderScan ast_scan;
        parse_ast_dump(ast_scan, ast_dump, import.location);
        if (used_prelude_retry) {
            ast_scan = filter_ast_scan_to_header(std::move(ast_scan), import, options);
        }
        scan = dedupe_scan(std::move(ast_scan));
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
        throw CompileError(import.location, scan_error_message(import, detail, clangxx_command()),
                           "dudu.native_header.scan_failed", unquoted(import.module_path));
    }
    parse_macro_dump(scan, macro_dump, import.location);
    if (!used_prelude_retry) {
        store_native_header_raw_cache(raw_cache, ast_dump, macro_dump);
    }
    cleanup();
    cache[key] = dedupe_scan(std::move(scan));
    return cache[key];
}
template <typename T>
std::vector<T> prefixed_names(const std::vector<T>& source, const std::string& prefix) {
    std::vector<T> out;
    out.reserve(source.size());
    for (T item : source) {
        if (!starts_with(item.name, prefix + ".")) {
            item.name = prefix + "." + item.name;
        }
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
        if (!starts_with(item.name, prefix + ".")) {
            item.name = prefix + "." + item.name;
        }
        if (item.native_spelling.empty()) {
            item.native_spelling = original;
            item.type_ref = named_type_ref(original, item.location);
        }
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
            append_unique_native_types(out.types, scan.types);
            append_unique_native_classes(out.classes, scan.classes);
            append_unique_native_values(out.values, scan.values);
            append_unique_native_functions(out.functions, scan.functions);
            append_unique_native_macros(out.macros, direct_macros(scan.macros));
            append_unique_native_namespaces(out.namespaces, scan.namespaces);
        } else {
            append_unique_native_types(out.types, scan.types);
            append_unique_native_classes(out.classes, scan.classes);
            append_unique_native_types(out.types, prefixed_type_names(scan.types, import.alias));
            append_unique_native_classes(out.classes, prefixed_names(scan.classes, import.alias));
            append_unique_native_values(out.values, prefixed_names(scan.values, import.alias));
            append_unique_native_functions(
                out.functions,
                prefixed_names(alias_visible_functions(scan, import, options), import.alias));
            append_unique_native_macros(out.macros, prefixed_names(scan.macros, import.alias));
            append_unique_native_namespaces(out.namespaces, scan.namespaces);
        }
    }
    return out;
}
void merge_native_headers(ModuleAst& module, const NativeHeaderOptions& options) {
    NativeHeaderScan scan = scan_native_headers(module, options);
    append_unique_native_types(module.native_types, scan.types);
    append_unique_native_values(module.native_values, scan.values);
    append_unique_native_functions(module.native_functions, scan.functions);
    append_unique_native_macros(module.native_macros, scan.macros);
    append_unique_native_namespaces(module.native_namespaces, scan.namespaces);
    append_unique_native_classes(module.native_classes, scan.classes);
}
std::vector<NativeTypeDecl> scan_native_header_types(const ModuleAst& module,
                                                     const NativeHeaderOptions& options) {
    return scan_native_headers(module, options).types;
}

void merge_native_header_types(ModuleAst& module, const NativeHeaderOptions& options) {
    merge_native_headers(module, options);
}

} // namespace dudu
