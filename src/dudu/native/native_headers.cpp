#include "dudu/native/native_headers.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/native/native_header_cache.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/native/native_header_parse.hpp"
#include "dudu/native/native_header_scan_command.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/project/project_driver.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
namespace dudu {
namespace {
bool is_foreign(const ImportDecl& import) {
    return import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCxx ||
           import.kind == ImportKind::ForeignCpp;
}
bool direct_import(const ImportDecl& import) {
    return import.alias.empty();
}
std::string unquoted_source_file(std::string value) {
    value = trim_copy(std::move(value));
    while (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                 (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}
bool requires_scan_success(const ImportDecl& import) {
    const std::filesystem::path header = native_header_unquoted(import.module_path);
    const std::string text = header.string();
    return header.is_absolute() || starts_with(text, ".") || starts_with(text, "/");
}
bool import_needs_source_dir_include(const ImportDecl& import, const NativeHeaderOptions& options) {
    const std::filesystem::path header = native_header_unquoted(import.module_path);
    if (header.is_absolute()) {
        return false;
    }
    const std::string text = header.string();
    if (starts_with(text, ".") || header.has_parent_path()) {
        return true;
    }
    std::error_code error;
    return std::filesystem::exists((options.source_dir / header).lexically_normal(), error) &&
           !error;
}
bool public_direct_macro_name(const std::string& name) {
    return !name.empty() &&
           (std::isupper(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_');
}
std::optional<std::filesystem::path>
normalized_existing_header_path(const ImportDecl& import, const NativeHeaderOptions& options) {
    const std::filesystem::path header = native_header_unquoted(import.module_path);
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
    const std::filesystem::path imported = native_header_unquoted(import.module_path);
    if (imported.has_parent_path()) {
        return source_path.string().ends_with(imported.lexically_normal().string());
    }
    return source_path.filename() == imported.filename();
}

std::string macro_raw_name(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

std::optional<SourceLocation> macro_definition_location(const std::filesystem::path& header,
                                                        const std::string& name) {
    std::ifstream in(header);
    if (!in) {
        return std::nullopt;
    }
    const std::regex define_pattern("^\\s*#\\s*define\\s+" + name + "(\\b|\\()");
    std::string line;
    int line_number = 1;
    while (std::getline(in, line)) {
        if (std::regex_search(line, define_pattern)) {
            const size_t column = line.find(name);
            return SourceLocation{
                .file = SourceFileName(header.string()),
                .line = line_number,
                .column = column == std::string::npos ? 1 : static_cast<int>(column + 1),
            };
        }
        ++line_number;
    }
    return std::nullopt;
}

void attach_macro_definition_locations(std::vector<NativeMacroDecl>& macros,
                                       const ImportDecl& import,
                                       const NativeHeaderOptions& options) {
    const std::optional<std::filesystem::path> header =
        normalized_existing_header_path(import, options);
    if (!header.has_value()) {
        return;
    }
    std::map<std::string, SourceLocation> locations;
    for (NativeMacroDecl& macro : macros) {
        const std::string raw_name = macro_raw_name(macro.name);
        if (const auto found = locations.find(raw_name); found != locations.end()) {
            macro.location = found->second;
            continue;
        }
        if (const std::optional<SourceLocation> location =
                macro_definition_location(*header, raw_name)) {
            locations.emplace(raw_name, *location);
            macro.location = *location;
        }
    }
}

bool source_file_belongs_to_import_family(const SourceLocation& location, const ImportDecl& import,
                                          const NativeHeaderOptions& options) {
    if (source_file_matches_header(location, import, options)) {
        return true;
    }
    const std::filesystem::path imported =
        std::filesystem::path(native_header_unquoted(import.module_path)).lexically_normal();
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

bool macro_function_stub(const NativeFunctionDecl& function) {
    if (function.identity.canonical_path != function.name ||
        function.return_native_spelling != "auto") {
        return false;
    }
    return std::ranges::all_of(function.param_native_spellings,
                               [](const std::string& param) { return param == "auto"; });
}

std::vector<NativeFunctionDecl> alias_visible_functions(const NativeHeaderScan& scan,
                                                        const ImportDecl& import,
                                                        const NativeHeaderOptions& options) {
    if (import.kind == ImportKind::ForeignCpp || import.kind == ImportKind::ForeignC ||
        import.kind == ImportKind::ForeignCxx) {
        return scan.functions;
    }
    std::vector<NativeFunctionDecl> out;
    for (const NativeFunctionDecl& function : scan.functions) {
        const bool macro_stub = macro_function_stub(function);
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
NativeHeaderScan scan_one_header(const ImportDecl& import, const NativeHeaderOptions& options,
                                 const std::string& flags) {
    static std::map<std::string, NativeHeaderScan> cache;
    const std::string key = native_header_scan_key(import, options, flags);
    NativeHeaderRawCache raw_cache = load_native_header_raw_cache(options, key);
    if (raw_cache.hit) {
        if (const auto found = cache.find(key); found != cache.end()) {
            return found->second;
        }
    } else {
        cache.erase(key);
    }
    if (const std::optional<NativeHeaderScan> scan =
            load_native_header_scan_cache(raw_cache, import.location)) {
        cache[key] = *scan;
        print_project_step(project_step_timings_enabled(), "native-scan-cache",
                           native_header_unquoted(import.module_path));
        return cache[key];
    }
    if (raw_cache.hit && load_native_header_raw_cache_payload(raw_cache)) {
        NativeHeaderScan scan;
        parse_ast_dump(scan, raw_cache.ast_dump, import.location);
        parse_macro_dump(scan, raw_cache.macro_dump, import.location);
        attach_macro_definition_locations(scan.macros, import, options);
        cache[key] = dedupe_scan(std::move(scan));
        store_native_header_scan_cache(raw_cache, cache[key]);
        print_project_step(project_step_timings_enabled(), "native-scan-raw",
                           native_header_unquoted(import.module_path));
        return cache[key];
    }
    const std::filesystem::path base = native_header_temp_base(options.source_dir);
    const std::filesystem::path cpp = base.string() + ".cpp";
    const std::filesystem::path ast = base.string() + ".ast";
    const std::filesystem::path macros = base.string() + ".macros";
    const std::filesystem::path deps = base.string() + ".d";
    const std::filesystem::path err = base.string() + ".err";
    const auto cleanup = [&]() {
        std::filesystem::remove(cpp);
        std::filesystem::remove(ast);
        std::filesystem::remove(macros);
        std::filesystem::remove(deps);
        std::filesystem::remove(err);
    };
    native_header_write_text(cpp, native_header_scanner_source_for_header(import, false));

    NativeHeaderScan scan;
    const std::string dependency_flags =
        " -MD -MF " + shell_quote_path(deps) + " -MT dudu_native_scan";
    std::string ast_dump = native_header_run_capture(
        native_header_clang_base_command(options, cpp, true, flags) + dependency_flags, ast, err);
    bool used_prelude_retry = false;
    if (ast_dump.empty()) {
        native_header_write_text(cpp, native_header_scanner_source_for_header(import, true));
        ast_dump = native_header_run_capture(
            native_header_clang_base_command(options, cpp, true, flags) + dependency_flags, ast,
            err);
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
    const std::string clang = native_header_clangxx_command();
    const std::string macro_cmd = shell_quote_arg(clang) +
                                  " -std=" + shell_quote_arg(options.config.cpp_std) +
                                  " -x c++ -dM -E " + shell_quote_path(cpp) + flags;
    const std::string macro_dump = native_header_run_capture(macro_cmd, macros, err);
    if (ast_dump.empty() && macro_dump.empty()) {
        if (!requires_scan_success(import)) {
            cleanup();
            return {};
        }
        const std::string detail = native_header_read_text(err);
        cleanup();
        throw CompileError(
            import.location,
            native_header_scan_error_message(import, detail, native_header_clangxx_command()),
            "dudu.native_header.scan_failed", native_header_unquoted(import.module_path));
    }
    parse_macro_dump(scan, macro_dump, import.location);
    attach_macro_definition_locations(scan.macros, import, options);
    if (!used_prelude_retry) {
        store_native_header_raw_cache(raw_cache, ast_dump, macro_dump,
                                      native_header_read_text(deps), cpp);
    }
    cleanup();
    cache[key] = dedupe_scan(std::move(scan));
    if (!used_prelude_retry) {
        store_native_header_scan_cache(raw_cache, cache[key]);
    }
    print_project_step(project_step_timings_enabled(), "native-scan-clang",
                       native_header_unquoted(import.module_path));
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
    NativeHeaderScan out;
    for (const ImportDecl& import : module.imports) {
        if (!is_foreign(import)) {
            continue;
        }
        const std::string flags =
            native_header_scanner_flags(options, import_needs_source_dir_include(import, options));
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
