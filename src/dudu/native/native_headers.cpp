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
#include "dudu/native/native_header_usr.hpp"
#include "dudu/project/project_driver.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
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
bool public_direct_macro_name(const std::string& name) {
    return !name.empty() &&
           (std::isupper(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_');
}
bool builtin_native_type_name(const std::string& name) {
    static const std::set<std::string> builtins = {
        "bool",  "char",  "i8",  "i16", "i32",  "i64", "u8",   "u16",  "u32",  "u64",
        "isize", "usize", "f32", "f64", "void", "str", "cstr", "auto", "None",
    };
    return builtins.contains(name);
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

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string trim_macro_doc_line(std::string text) {
    text = trim_copy(std::move(text));
    if (text.starts_with("///")) {
        return trim_copy(text.substr(3));
    }
    if (text.starts_with("//")) {
        return trim_copy(text.substr(2));
    }
    if (text.starts_with("/**")) {
        text = trim_copy(text.substr(3));
    } else if (text.starts_with("/*")) {
        text = trim_copy(text.substr(2));
    }
    if (text.ends_with("*/")) {
        text = trim_copy(text.substr(0, text.size() - 2));
    }
    if (text.starts_with("*")) {
        text = trim_copy(text.substr(1));
    }
    return text;
}

std::string macro_doc_before_line(const std::vector<std::string>& lines, size_t define_index) {
    std::vector<std::string> pending;
    bool in_block = false;
    for (size_t cursor = define_index; cursor > 0;) {
        --cursor;
        std::string line = trim_copy(lines[cursor]);
        if (line.empty()) {
            break;
        }
        if (in_block) {
            pending.push_back(trim_macro_doc_line(line));
            if (line.find("/*") != std::string::npos) {
                break;
            }
            continue;
        }
        if (line.starts_with("//")) {
            pending.push_back(trim_macro_doc_line(line));
            continue;
        }
        if (line.find("*/") != std::string::npos) {
            pending.push_back(trim_macro_doc_line(line));
            if (line.find("/*") != std::string::npos) {
                break;
            }
            in_block = true;
            continue;
        }
        break;
    }
    std::reverse(pending.begin(), pending.end());
    std::string out;
    for (const std::string& line : pending) {
        if (line.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += "\n";
        }
        out += line;
    }
    return out;
}

std::optional<SourceLocation> macro_definition_location(const std::filesystem::path& header,
                                                        const std::vector<std::string>& lines,
                                                        const std::string& name) {
    const std::regex define_pattern("^\\s*#\\s*define\\s+" + name + "(\\b|\\()");
    for (size_t index = 0; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        if (std::regex_search(line, define_pattern)) {
            const size_t column = line.find(name);
            return SourceLocation{
                .file = SourceFileName(header.string()),
                .line = static_cast<int>(index + 1),
                .column = column == std::string::npos ? 1 : static_cast<int>(column + 1),
            };
        }
    }
    return std::nullopt;
}

void attach_macro_definition_locations(NativeHeaderScan& scan, const ImportDecl& import,
                                       const NativeHeaderOptions& options) {
    const std::optional<std::filesystem::path> header =
        normalized_existing_header_path(import, options);
    if (!header.has_value()) {
        return;
    }
    const std::vector<std::string> lines = read_lines(*header);
    std::map<std::string, SourceLocation> locations;
    std::map<std::string, std::string> docs;
    for (NativeMacroDecl& macro : scan.macros) {
        const std::string raw_name = macro_raw_name(macro.name);
        if (const auto found = locations.find(raw_name); found != locations.end()) {
            macro.location = found->second;
            if (const auto doc = docs.find(raw_name); doc != docs.end()) {
                macro.doc_comment = doc->second;
            }
            continue;
        }
        if (const std::optional<SourceLocation> location =
                macro_definition_location(*header, lines, raw_name)) {
            locations.emplace(raw_name, *location);
            macro.location = *location;
            const std::string doc =
                macro_doc_before_line(lines, static_cast<size_t>(location->line - 1));
            docs.emplace(raw_name, doc);
            macro.doc_comment = doc;
        }
    }
    for (NativeFunctionDecl& function : scan.functions) {
        const std::string raw_name = macro_raw_name(function.name);
        const auto doc = docs.find(raw_name);
        if (doc == docs.end() || doc->second.empty()) {
            continue;
        }
        const bool macro_stub =
            function.identity.canonical_path == raw_name &&
            function.return_native_spelling == "auto" &&
            std::ranges::all_of(function.param_native_spellings,
                                [](const std::string& param) { return param == "auto"; });
        if (macro_stub) {
            function.doc_comment = doc->second;
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

std::set<std::string> native_type_names(const NativeHeaderScan& scan) {
    std::set<std::string> names;
    for (const NativeTypeDecl& type : scan.types) {
        names.insert(type.name);
    }
    for (const ClassDecl& klass : scan.classes) {
        names.insert(klass.name);
    }
    return names;
}

void prefix_type_ref(TypeRef& type, const std::set<std::string>& names, const std::string& prefix) {
    if (names.contains(type.name.str()) && !builtin_native_type_name(type.name.str()) &&
        !type.name.starts_with(prefix + ".")) {
        type.name = prefix + "." + type.name.str();
    }
    for (TypeRef& child : type.children) {
        prefix_type_ref(child, names, prefix);
    }
}

void prefix_param_type_refs(std::vector<ParamDecl>& params, const std::set<std::string>& names,
                            const std::string& prefix) {
    for (ParamDecl& param : params) {
        prefix_type_ref(param.type_ref, names, prefix);
    }
}

void prefix_native_type_refs(NativeValueDecl& value, const std::set<std::string>& names,
                             const std::string& prefix) {
    prefix_type_ref(value.type_ref, names, prefix);
}

void prefix_native_type_refs(NativeTypeDecl& type, const std::set<std::string>& names,
                             const std::string& prefix) {
    prefix_type_ref(type.type_ref, names, prefix);
}

void prefix_native_type_refs(NativeFunctionDecl& fn, const std::set<std::string>& names,
                             const std::string& prefix) {
    for (TypeRef& param : fn.param_type_refs) {
        prefix_type_ref(param, names, prefix);
    }
    prefix_type_ref(fn.return_type_ref, names, prefix);
}

void prefix_native_type_refs(ClassDecl& klass, const std::set<std::string>& names,
                             const std::string& prefix) {
    for (BaseClassDecl& base : klass.base_class_refs) {
        prefix_type_ref(base.type_ref, names, prefix);
    }
    for (FieldDecl& field : klass.fields) {
        prefix_type_ref(field.type_ref, names, prefix);
    }
    for (ConstDecl& constant : klass.constants) {
        prefix_type_ref(constant.type_ref, names, prefix);
    }
    for (ConstDecl& field : klass.static_fields) {
        prefix_type_ref(field.type_ref, names, prefix);
    }
    for (FunctionDecl& method : klass.methods) {
        prefix_type_ref(method.receiver_type_ref, names, prefix);
        prefix_param_type_refs(method.params, names, prefix);
        prefix_type_ref(method.return_type_ref, names, prefix);
    }
}

template <typename T>
std::vector<T> prefixed_type_refs(std::vector<T> items, const std::set<std::string>& names,
                                  const std::string& prefix) {
    for (T& item : items) {
        prefix_native_type_refs(item, names, prefix);
    }
    return items;
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
            label += ",";
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
            attach_macro_definition_locations(scan, import, options);
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
        NativeHeaderScan fallback;
        for (const ImportDecl& import : imports) {
            NativeHeaderScan isolated = scan_headers(std::span<const ImportDecl>(&import, 1),
                                                     options, flags, include_source_dir);
            append_unique_native_types(fallback.types, isolated.types);
            append_unique_native_classes(fallback.classes, isolated.classes);
            append_unique_native_values(fallback.values, isolated.values);
            append_unique_native_functions(fallback.functions, isolated.functions);
            append_unique_native_macros(fallback.macros, isolated.macros);
            append_unique_native_namespaces(fallback.namespaces, isolated.namespaces);
        }
        return fallback;
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
        attach_macro_definition_locations(scan, import, options);
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
                                                const std::string& prefix,
                                                const std::set<std::string>& c_record_names = {}) {
    std::vector<NativeTypeDecl> out;
    out.reserve(source.size());
    for (NativeTypeDecl item : source) {
        const std::string original = item.name;
        if (!starts_with(item.name, prefix + ".")) {
            item.name = prefix + "." + item.name;
        }
        if (item.native_spelling.empty()) {
            item.native_spelling =
                c_record_names.contains(original) ? "struct " + original : original;
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
        NativeHeaderScan scan = scan_headers(direct_imports, options, flags, include_source_dir);
        append_unique_native_types(out.types, scan.types);
        append_unique_native_classes(out.classes, scan.classes);
        append_unique_native_values(out.values, scan.values);
        append_unique_native_functions(out.functions, scan.functions);
        append_unique_native_macros(out.macros, direct_macros(scan.macros));
        append_unique_native_namespaces(out.namespaces, scan.namespaces);
    }

    for (const ImportDecl& import : module.imports) {
        if (!is_foreign(import) || direct_import(import)) {
            continue;
        }
        const std::string flags =
            native_header_scanner_flags(options, import_needs_source_dir_include(import, options));
        const bool include_source_dir = import_needs_source_dir_include(import, options);
        NativeHeaderScan scan = scan_one_header(import, options, flags, include_source_dir);
        const std::set<std::string> type_names = native_type_names(scan);
        std::set<std::string> c_record_names;
        if (import.kind == ImportKind::ForeignC) {
            for (const ClassDecl& klass : scan.classes) {
                c_record_names.insert(klass.name);
            }
        }
        append_unique_native_types(
            out.types,
            prefixed_type_refs(prefixed_type_names(scan.types, import.alias, c_record_names),
                               type_names, import.alias));
        append_unique_native_classes(out.classes,
                                     prefixed_type_refs(prefixed_names(scan.classes, import.alias),
                                                        type_names, import.alias));
        append_unique_native_values(out.values,
                                    prefixed_type_refs(prefixed_names(scan.values, import.alias),
                                                       type_names, import.alias));
        append_unique_native_functions(
            out.functions,
            prefixed_type_refs(
                prefixed_names(alias_visible_functions(scan, import, options), import.alias),
                type_names, import.alias));
        append_unique_native_macros(out.macros, prefixed_names(scan.macros, import.alias));
        append_unique_native_namespaces(out.namespaces,
                                        prefixed_names(scan.namespaces, import.alias));
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
