#include "dudu/native/native_headers.hpp"

#include "dudu/core/text.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/native/native_header_cache.hpp"
#include "dudu/native/native_header_import_view.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/native/native_header_parse.hpp"
#include "dudu/native/native_header_scan_command.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/native/native_header_usr.hpp"
#include "dudu/project/project_driver.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace dudu {
namespace {

using Clock = std::chrono::steady_clock;

void print_native_phase(std::string_view label, const Clock::time_point start) {
    if (!project_step_timings_enabled()) {
        return;
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start);
    print_project_step(true, std::string(label),
                       std::to_string(static_cast<double>(elapsed.count()) / 1000.0) + " ms");
}

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
    if (import.native_include_style == NativeIncludeStyle::System) {
        return false;
    }
    const std::filesystem::path header = native_header_unquoted(import.module_path);
    const std::string text = header.string();
    return header.is_absolute() || starts_with(text, ".") || starts_with(text, "/");
}

bool import_needs_source_dir_include(const ImportDecl& import, const NativeHeaderOptions& options) {
    if (import.native_include_style == NativeIncludeStyle::System) {
        return false;
    }
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

bool source_file_matches_header(const SourceLocation& location, const ImportDecl& import,
                                const NativeHeaderOptions& options) {
    const std::string file = unquoted_source_file(location.file.str());
    if (file.empty()) {
        return false;
    }
    const std::filesystem::path source_path = std::filesystem::path(file).lexically_normal();
    if (const auto header_path = resolve_existing_native_header_path(import, options)) {
        return source_path == *header_path;
    }
    const std::filesystem::path imported = native_header_unquoted(import.module_path);
    if (imported.has_parent_path()) {
        return source_path.string().ends_with(imported.lexically_normal().string());
    }
    return source_path.filename() == imported.filename();
}

template <typename T>
std::vector<T> filter_native_decls_to_headers(const std::vector<T>& source,
                                              std::span<const ImportDecl> imports,
                                              const NativeHeaderOptions& options) {
    std::vector<T> out;
    for (const T& item : source) {
        if (std::ranges::any_of(imports, [&](const ImportDecl& import) {
                return source_file_matches_header(item.location, import, options);
            })) {
            out.push_back(item);
        }
    }
    return out;
}

NativeHeaderScan filter_ast_scan_to_headers(NativeHeaderScan scan,
                                            std::span<const ImportDecl> imports,
                                            const NativeHeaderOptions& options) {
    scan.types = filter_native_decls_to_headers(scan.types, imports, options);
    scan.values = filter_native_decls_to_headers(scan.values, imports, options);
    scan.functions = filter_native_decls_to_headers(scan.functions, imports, options);
    scan.namespaces = filter_native_decls_to_headers(scan.namespaces, imports, options);
    scan.classes = filter_native_decls_to_headers(scan.classes, imports, options);
    return scan;
}

std::string native_scan_label(std::span<const ImportDecl> imports) {
    std::string label;
    for (const ImportDecl& import : imports) {
        if (!label.empty()) {
            label += ',';
        }
        label += native_header_unquoted(import.module_path);
    }
    return label;
}

NativeHeaderScan scan_headers(std::span<const ImportDecl> imports,
                              const NativeHeaderOptions& options, const std::string& flags,
                              bool include_source_dir) {
    if (imports.empty()) {
        return {};
    }
    static std::recursive_mutex scan_mutex;
    const std::lock_guard lock(scan_mutex);
    static std::map<std::string, NativeHeaderScan> cache;

    const SourceLocation location = imports.front().location;
    const std::string label = native_scan_label(imports);
    const std::string key = native_header_scan_key(imports, options, flags);
    const Clock::time_point cache_lookup_start = Clock::now();
    NativeHeaderRawCache raw_cache = load_native_header_raw_cache(options, key);
    print_native_phase("native.cache-lookup", cache_lookup_start);
    if (raw_cache.hit) {
        if (const auto found = cache.find(key); found != cache.end()) {
            return found->second;
        }
    } else {
        cache.erase(key);
    }

    const Clock::time_point scan_cache_start = Clock::now();
    const std::optional<NativeHeaderScan> scan_cache =
        load_native_header_scan_cache(raw_cache, location);
    print_native_phase("native.scan-deserialize", scan_cache_start);
    if (scan_cache) {
        cache[key] = *scan_cache;
        print_project_step(project_step_timings_enabled(), "native-scan-cache", label);
        return cache[key];
    }

    if (raw_cache.hit && load_native_header_raw_cache_payload(raw_cache)) {
        const Clock::time_point raw_parse_start = Clock::now();
        NativeHeaderScan scan;
        parse_ast_dump(scan, raw_cache.ast_dump, location,
                       NativeCursorIdentityIndex::deserialize(raw_cache.identity_dump));
        parse_macro_dump(scan, raw_cache.macro_dump, location);
        for (const ImportDecl& import : imports) {
            attach_native_macro_definition_locations(scan, import, options);
        }
        cache[key] = dedupe_scan(std::move(scan));
        print_native_phase("native.raw-parse", raw_parse_start);
        const Clock::time_point store_start = Clock::now();
        store_native_header_scan_cache(raw_cache, cache[key]);
        print_native_phase("native.scan-serialize", store_start);
        print_project_step(project_step_timings_enabled(), "native-scan-raw", label);
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
    native_header_write_text(cpp, native_header_scanner_source_for_headers(imports, false));

    NativeHeaderScan scan;
    const Clock::time_point identity_start = Clock::now();
    NativeCursorIdentityIndex identities =
        scan_native_cursor_identities(cpp, options, include_source_dir);
    print_native_phase("native.libclang", identity_start);
    const std::string dependency_flags =
        " -MD -MF " + shell_quote_path(deps) + " -MT dudu_native_scan";
    const Clock::time_point ast_dump_start = Clock::now();
    std::string ast_dump = native_header_run_capture(
        native_header_clang_base_command(options, cpp, true, flags) + dependency_flags, ast, err);
    print_native_phase("native.ast-dump", ast_dump_start);

    bool used_prelude_retry = false;
    if (ast_dump.empty()) {
        native_header_write_text(cpp, native_header_scanner_source_for_headers(imports, true));
        const Clock::time_point retry_identity_start = Clock::now();
        identities = scan_native_cursor_identities(cpp, options, include_source_dir);
        print_native_phase("native.libclang-retry", retry_identity_start);
        const Clock::time_point retry_ast_start = Clock::now();
        ast_dump = native_header_run_capture(
            native_header_clang_base_command(options, cpp, true, flags) + dependency_flags, ast,
            err);
        print_native_phase("native.ast-dump-retry", retry_ast_start);
        used_prelude_retry = !ast_dump.empty();
    }

    if (ast_dump.empty() && imports.size() > 1) {
        cleanup();
        NativeHeaderScan isolated_scan;
        for (const ImportDecl& import : imports) {
            NativeHeaderScan isolated = scan_headers(std::span<const ImportDecl>(&import, 1),
                                                     options, flags, include_source_dir);
            append_unique_native_types(isolated_scan.types, isolated.types);
            append_unique_native_classes(isolated_scan.classes, isolated.classes);
            append_unique_native_values(isolated_scan.values, isolated.values);
            append_unique_native_functions(isolated_scan.functions, isolated.functions);
            append_unique_native_macros(isolated_scan.macros, isolated.macros);
            append_unique_native_namespaces(isolated_scan.namespaces, isolated.namespaces);
        }
        return isolated_scan;
    }

    if (!ast_dump.empty()) {
        const Clock::time_point ast_parse_start = Clock::now();
        NativeHeaderScan ast_scan;
        parse_ast_dump(ast_scan, ast_dump, location, identities);
        if (used_prelude_retry) {
            ast_scan = filter_ast_scan_to_headers(std::move(ast_scan), imports, options);
        }
        scan = dedupe_scan(std::move(ast_scan));
        print_native_phase("native.ast-parse", ast_parse_start);
    }

    const std::string clang = native_header_clangxx_command();
    const std::string macro_cmd = shell_quote_arg(clang) +
                                  " -std=" + shell_quote_arg(options.config.cpp_std) +
                                  " -x c++ -dM -E " + shell_quote_path(cpp) + flags;
    const Clock::time_point macro_dump_start = Clock::now();
    const std::string macro_dump = native_header_run_capture(macro_cmd, macros, err);
    print_native_phase("native.macro-dump", macro_dump_start);
    if (ast_dump.empty() && macro_dump.empty()) {
        const auto required = std::ranges::find_if(imports, requires_scan_success);
        if (required == imports.end()) {
            cleanup();
            return {};
        }
        const std::string detail = native_header_read_text(err);
        cleanup();
        throw CompileError(
            required->location,
            native_header_scan_error_message(*required, detail, native_header_clangxx_command()),
            "dudu.native_header.scan_failed", native_header_unquoted(required->module_path));
    }

    const Clock::time_point macro_parse_start = Clock::now();
    parse_macro_dump(scan, macro_dump, location);
    for (const ImportDecl& import : imports) {
        attach_native_macro_definition_locations(scan, import, options);
    }
    print_native_phase("native.macro-parse", macro_parse_start);
    if (!used_prelude_retry) {
        const Clock::time_point raw_store_start = Clock::now();
        store_native_header_raw_cache(raw_cache, ast_dump, macro_dump, identities.serialize(),
                                      native_header_read_text(deps), cpp);
        print_native_phase("native.raw-serialize", raw_store_start);
    }

    cleanup();
    cache[key] = dedupe_scan(std::move(scan));
    if (!used_prelude_retry) {
        const Clock::time_point scan_store_start = Clock::now();
        store_native_header_scan_cache(raw_cache, cache[key]);
        print_native_phase("native.scan-serialize", scan_store_start);
    }
    if (project_step_timings_enabled()) {
        print_project_step(true, "native.clang-passes", used_prelude_retry ? "5" : "3");
    }
    print_project_step(project_step_timings_enabled(), "native-scan-clang", label);
    return cache[key];
}

NativeHeaderScan scan_one_header(const ImportDecl& import, const NativeHeaderOptions& options,
                                 const std::string& flags, bool include_source_dir) {
    return scan_headers(std::span<const ImportDecl>(&import, 1), options, flags,
                        include_source_dir);
}

void append_scan(NativeHeaderScan& target, const NativeHeaderScan& source) {
    append_unique_native_types(target.types, source.types);
    append_unique_native_classes(target.classes, source.classes);
    append_unique_native_values(target.values, source.values);
    append_unique_native_functions(target.functions, source.functions);
    append_unique_native_macros(target.macros, source.macros);
    append_unique_native_namespaces(target.namespaces, source.namespaces);
}

} // namespace

NativeHeaderScan scan_native_headers(const ModuleAst& module, const NativeHeaderOptions& options) {
    NativeHeaderScan out;
    std::vector<ImportDecl> direct_imports;
    for (const ImportDecl& import : module.imports) {
        if (is_foreign(import) && direct_import(import)) {
            direct_imports.push_back(import);
        }
    }
    if (!direct_imports.empty()) {
        const bool include_source_dir =
            std::ranges::any_of(direct_imports, [&](const ImportDecl& import) {
                return import_needs_source_dir_include(import, options);
            });
        const std::string flags = native_header_scanner_flags(options, include_source_dir);
        append_scan(out, direct_native_import_view(
                             scan_headers(direct_imports, options, flags, include_source_dir)));
    }

    for (const ImportDecl& import : module.imports) {
        if (!is_foreign(import) || direct_import(import)) {
            continue;
        }
        const bool include_source_dir = import_needs_source_dir_include(import, options);
        const std::string flags = native_header_scanner_flags(options, include_source_dir);
        append_scan(out, aliased_native_import_view(
                             scan_one_header(import, options, flags, include_source_dir), import));
    }
    return out;
}

void merge_native_headers(ModuleAst& module, const NativeHeaderOptions& options) {
    const NativeHeaderScan scan = scan_native_headers(module, options);
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
