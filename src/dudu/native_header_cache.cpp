#include "dudu/native_header_cache.hpp"

#include "dudu/file_io.hpp"
#include "dudu/native_header_cache_deps.hpp"
#include "dudu/native_header_cache_format.hpp"
#include "dudu/project_config.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

constexpr std::string_view kScanCacheVersion = "dudu-native-scan-v4";

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
    return std::to_string(std::hash<std::string>{}(key));
}

NativeSymbolId symbol_id(const std::vector<std::string>& fields, size_t usr_index,
                         size_t path_index) {
    return {.usr = fields[usr_index], .canonical_path = fields[path_index]};
}

std::vector<std::string> location_fields(const SourceLocation& location) {
    return {location.file.str(), std::to_string(location.line), std::to_string(location.column)};
}

void append_location_fields(std::vector<std::string>& fields, const SourceLocation& location) {
    std::vector<std::string> loc = location_fields(location);
    fields.insert(fields.end(), loc.begin(), loc.end());
}

SourceLocation cached_location(const std::vector<std::string>& fields, size_t index,
                               const SourceLocation& default_location) {
    if (fields.size() < index + 3) {
        return default_location;
    }
    return {.file = SourceFileName(fields[index]),
            .line = std::stoi(fields[index + 1]),
            .column = std::stoi(fields[index + 2])};
}

ParamDecl cached_param(std::string name, const std::string& type, const SourceLocation& location) {
    return {
        .name = std::move(name), .type_ref = cached_type_ref(type, location), .location = location};
}

void write_function_record(std::ostream& out, std::string_view tag, const FunctionDecl& fn) {
    std::vector<std::string> fields = {fn.name,
                                       fn.cpp_name,
                                       fn.native_identity.usr,
                                       fn.native_identity.canonical_path,
                                       native_cache_join_strings(fn.generic_params),
                                       cached_type_text(fn.return_type_ref)};
    append_location_fields(fields, fn.location);
    fields.push_back(std::to_string(fn.params.size()));
    for (const ParamDecl& param : fn.params) {
        fields.push_back(param.name);
        fields.push_back(cached_type_text(param.type_ref));
    }
    write_record(out, tag, fields);
}

std::optional<FunctionDecl> read_function_record(const std::vector<std::string>& fields,
                                                 const SourceLocation& location) {
    if (fields.size() < 10) {
        return std::nullopt;
    }
    const SourceLocation function_location = cached_location(fields, 6, location);
    FunctionDecl fn;
    fn.name = fields[0];
    fn.cpp_name = fields[1];
    fn.native_identity = symbol_id(fields, 2, 3);
    fn.generic_params = native_cache_split_strings(fields[4]);
    fn.return_type_ref = cached_type_ref(fields[5], function_location);
    fn.location = function_location;
    const size_t param_count = static_cast<size_t>(std::stoull(fields[9]));
    if (fields.size() != 10 + param_count * 2) {
        return std::nullopt;
    }
    for (size_t i = 0; i < param_count; ++i) {
        fn.params.push_back(
            cached_param(fields[10 + i * 2], fields[11 + i * 2], function_location));
    }
    return fn;
}

std::filesystem::path scan_cache_path(const NativeHeaderRawCache& cache) {
    return cache.base.string() + ".scan";
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
    const std::filesystem::path deps = cache.base.string() + ".deps";
    if (!std::filesystem::exists(ast) || !std::filesystem::exists(macros) ||
        !std::filesystem::exists(deps)) {
        return cache;
    }
    cache.dependencies = read_text(deps);
    if (!native_header_dependency_stamps_current(cache.dependencies)) {
        return cache;
    }
    cache.ast_dump = read_text(ast);
    cache.macro_dump = read_text(macros);
    cache.hit = !cache.ast_dump.empty() || !cache.macro_dump.empty();
    return cache;
}

void store_native_header_raw_cache(const NativeHeaderRawCache& cache, const std::string& ast_dump,
                                   const std::string& macro_dump, const std::string& dependencies,
                                   const std::filesystem::path& generated_source) {
    std::filesystem::create_directories(cache.base.parent_path());
    write_text(cache.base.string() + ".ast", ast_dump);
    write_text(cache.base.string() + ".macros", macro_dump);
    write_text(cache.base.string() + ".deps",
               native_header_dependency_stamps_from_makefile(dependencies, generated_source));
}

std::optional<NativeHeaderScan> load_native_header_scan_cache(const NativeHeaderRawCache& cache,
                                                              const SourceLocation& location) {
    if (!native_header_dependency_stamps_current(cache.dependencies)) {
        return std::nullopt;
    }
    const std::string text = read_text(scan_cache_path(cache));
    if (text.empty()) {
        return std::nullopt;
    }
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != kScanCacheVersion) {
        return std::nullopt;
    }
    NativeHeaderScan scan;
    ClassDecl* current_class = nullptr;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const auto parsed = parse_record(line);
        if (!parsed) {
            return std::nullopt;
        }
        const std::string& tag = parsed->first;
        const std::vector<std::string>& fields = parsed->second;
        if (tag == "NT" && fields.size() == 8) {
            const SourceLocation decl_location = cached_location(fields, 5, location);
            scan.types.push_back({.name = fields[0],
                                  .native_spelling = fields[1],
                                  .type_ref = cached_type_ref(fields[2], decl_location),
                                  .identity = symbol_id(fields, 3, 4),
                                  .location = decl_location});
        } else if (tag == "NV" && fields.size() == 9) {
            const SourceLocation decl_location = cached_location(fields, 6, location);
            scan.values.push_back({.name = fields[0],
                                   .native_spelling = fields[1],
                                   .type_ref = cached_type_ref(fields[2], decl_location),
                                   .enum_constant = fields[3] == "1",
                                   .identity = symbol_id(fields, 4, 5),
                                   .location = decl_location});
        } else if (tag == "NF" && fields.size() == 13) {
            const SourceLocation decl_location = cached_location(fields, 10, location);
            scan.functions.push_back(
                {.name = fields[0],
                 .template_params = native_cache_split_strings(fields[1]),
                 .param_native_spellings = native_cache_split_strings(fields[2]),
                 .param_type_refs = cached_type_refs(fields[3], decl_location),
                 .return_native_spelling = fields[4],
                 .return_type_ref = cached_type_ref(fields[5], decl_location),
                 .min_params = std::stoi(fields[6]),
                 .variadic = fields[7] == "1",
                 .identity = symbol_id(fields, 8, 9),
                 .location = decl_location});
        } else if (tag == "NM" && fields.size() == 8) {
            const SourceLocation decl_location = cached_location(fields, 5, location);
            scan.macros.push_back({.name = fields[0],
                                   .arity = std::stoi(fields[1]),
                                   .function_like = fields[2] == "1",
                                   .identity = symbol_id(fields, 3, 4),
                                   .location = decl_location});
        } else if (tag == "NN" && fields.size() == 6) {
            scan.namespaces.push_back({.name = fields[0],
                                       .identity = symbol_id(fields, 1, 2),
                                       .location = cached_location(fields, 3, location)});
        } else if (tag == "CLS" && fields.size() == 9) {
            const SourceLocation decl_location = cached_location(fields, 6, location);
            ClassDecl klass;
            klass.name = fields[0];
            klass.cpp_name = fields[1];
            klass.identity = symbol_id(fields, 2, 3);
            klass.generic_params = native_cache_split_strings(fields[4]);
            klass.origin_module = fields[5];
            klass.location = decl_location;
            scan.classes.push_back(std::move(klass));
            current_class = &scan.classes.back();
        } else if (tag == "BASE" && fields.size() == 4 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 1, location);
            current_class->base_class_refs.push_back(
                {.type_ref = cached_type_ref(fields[0], decl_location), .location = decl_location});
        } else if (tag == "FLD" && fields.size() == 5 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 2, location);
            current_class->fields.push_back({.name = fields[0],
                                             .type_ref = cached_type_ref(fields[1], decl_location),
                                             .value_expr = {},
                                             .location = decl_location});
        } else if (tag == "MET" && current_class != nullptr) {
            const std::optional<FunctionDecl> fn = read_function_record(fields, location);
            if (!fn) {
                return std::nullopt;
            }
            current_class->methods.push_back(*fn);
        } else if (tag == "ENDCLS") {
            current_class = nullptr;
        } else {
            return std::nullopt;
        }
    }
    return scan;
}

void store_native_header_scan_cache(const NativeHeaderRawCache& cache,
                                    const NativeHeaderScan& scan) {
    std::ostringstream out;
    out << kScanCacheVersion << '\n';
    for (const NativeTypeDecl& item : scan.types) {
        std::vector<std::string> fields = {item.name, item.native_spelling,
                                           cached_type_text(item.type_ref), item.identity.usr,
                                           item.identity.canonical_path};
        append_location_fields(fields, item.location);
        write_record(out, "NT", fields);
    }
    for (const NativeValueDecl& item : scan.values) {
        std::vector<std::string> fields = {item.name,
                                           item.native_spelling,
                                           cached_type_text(item.type_ref),
                                           item.enum_constant ? "1" : "0",
                                           item.identity.usr,
                                           item.identity.canonical_path};
        append_location_fields(fields, item.location);
        write_record(out, "NV", fields);
    }
    for (const NativeFunctionDecl& item : scan.functions) {
        std::vector<std::string> fields = {
            item.name,
            native_cache_join_strings(item.template_params),
            native_cache_join_strings(item.param_native_spellings),
            native_cache_join_strings(cached_type_texts(item.param_type_refs)),
            item.return_native_spelling,
            cached_type_text(item.return_type_ref),
            std::to_string(item.min_params),
            item.variadic ? "1" : "0",
            item.identity.usr,
            item.identity.canonical_path};
        append_location_fields(fields, item.location);
        write_record(out, "NF", fields);
    }
    for (const NativeMacroDecl& item : scan.macros) {
        std::vector<std::string> fields = {item.name, std::to_string(item.arity),
                                           item.function_like ? "1" : "0", item.identity.usr,
                                           item.identity.canonical_path};
        append_location_fields(fields, item.location);
        write_record(out, "NM", fields);
    }
    for (const NativeNamespaceDecl& item : scan.namespaces) {
        std::vector<std::string> fields = {item.name, item.identity.usr,
                                           item.identity.canonical_path};
        append_location_fields(fields, item.location);
        write_record(out, "NN", fields);
    }
    for (const ClassDecl& klass : scan.classes) {
        std::vector<std::string> fields = {klass.name,
                                           klass.cpp_name,
                                           klass.identity.usr,
                                           klass.identity.canonical_path,
                                           native_cache_join_strings(klass.generic_params),
                                           klass.origin_module};
        append_location_fields(fields, klass.location);
        write_record(out, "CLS", fields);
        for (const BaseClassDecl& base : klass.base_class_refs) {
            std::vector<std::string> base_fields = {cached_type_text(base.type_ref)};
            append_location_fields(base_fields, base.location);
            write_record(out, "BASE", base_fields);
        }
        for (const FieldDecl& field : klass.fields) {
            std::vector<std::string> field_fields = {field.name, cached_type_text(field.type_ref)};
            append_location_fields(field_fields, field.location);
            write_record(out, "FLD", field_fields);
        }
        for (const FunctionDecl& method : klass.methods) {
            write_function_record(out, "MET", method);
        }
        write_record(out, "ENDCLS", {});
    }
    std::filesystem::create_directories(cache.base.parent_path());
    write_text(scan_cache_path(cache), out.str());
}

} // namespace dudu
