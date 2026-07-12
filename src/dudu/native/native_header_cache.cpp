#include "dudu/native/native_header_cache.hpp"

#include "dudu/core/file_io.hpp"
#include "dudu/native/native_header_cache_deps.hpp"
#include "dudu/native/native_header_cache_format.hpp"
#include "dudu/project/project_config.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

constexpr std::string_view kScanCacheVersion = "dudu-native-scan-v29";

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

std::optional<TypeLayout> cached_layout(const std::vector<std::string>& fields, size_t index) {
    if (fields.size() < index + 2 || fields[index].empty() || fields[index + 1].empty()) {
        return std::nullopt;
    }
    return TypeLayout{.size = static_cast<size_t>(std::stoull(fields[index])),
                      .alignment = static_cast<size_t>(std::stoull(fields[index + 1]))};
}

void append_layout_fields(std::vector<std::string>& fields,
                          const std::optional<TypeLayout>& layout) {
    fields.push_back(layout ? std::to_string(layout->size) : "");
    fields.push_back(layout ? std::to_string(layout->alignment) : "");
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
                                       cached_type_text(fn.return_type_ref),
                                       fn.doc_comment};
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
    if (fields.size() < 11) {
        return std::nullopt;
    }
    const SourceLocation function_location = cached_location(fields, 7, location);
    FunctionDecl fn;
    fn.name = fields[0];
    fn.cpp_name = fields[1];
    fn.native_identity = symbol_id(fields, 2, 3);
    fn.generic_params = native_cache_split_strings(fields[4]);
    fn.return_type_ref = cached_type_ref(fields[5], function_location);
    fn.doc_comment = fields[6];
    fn.location = function_location;
    const size_t param_count = static_cast<size_t>(std::stoull(fields[10]));
    if (fields.size() != 11 + param_count * 2) {
        return std::nullopt;
    }
    for (size_t i = 0; i < param_count; ++i) {
        fn.params.push_back(
            cached_param(fields[11 + i * 2], fields[12 + i * 2], function_location));
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
        if (tag == "NT" && fields.size() == 14) {
            const SourceLocation decl_location = cached_location(fields, 6, location);
            NativeTypeDecl type{.name = fields[0],
                                .native_spelling = fields[1],
                                .type_ref = cached_type_ref(fields[2], decl_location),
                                .identity = symbol_id(fields, 3, 4),
                                .layout = cached_layout(fields, 12),
                                .location = decl_location,
                                .doc_comment = fields[5]};
            type.generic_params = native_cache_split_strings(fields[9]);
            if (!fields[10].empty()) {
                type.generic_min_args = static_cast<size_t>(std::stoull(fields[10]));
            }
            type.generic_default_args = cached_type_refs(fields[11], decl_location);
            scan.types.push_back(std::move(type));
        } else if (tag == "NV" && fields.size() == 10) {
            const SourceLocation decl_location = cached_location(fields, 7, location);
            scan.values.push_back({.name = fields[0],
                                   .native_spelling = fields[1],
                                   .type_ref = cached_type_ref(fields[2], decl_location),
                                   .enum_constant = fields[3] == "1",
                                   .identity = symbol_id(fields, 4, 5),
                                   .location = decl_location,
                                   .doc_comment = fields[6]});
        } else if (tag == "NF" && fields.size() == 15) {
            const SourceLocation decl_location = cached_location(fields, 12, location);
            scan.functions.push_back(
                {.name = fields[0],
                 .template_params = native_cache_split_strings(fields[1]),
                 .param_names = native_cache_split_strings(fields[2]),
                 .param_native_spellings = native_cache_split_strings(fields[3]),
                 .param_type_refs = cached_type_refs(fields[4], decl_location),
                 .return_native_spelling = fields[5],
                 .return_type_ref = cached_type_ref(fields[6], decl_location),
                 .min_params = std::stoi(fields[7]),
                 .variadic = fields[8] == "1",
                 .identity = symbol_id(fields, 9, 10),
                 .location = decl_location,
                 .doc_comment = fields[11]});
        } else if (tag == "NM" && fields.size() == 9) {
            const SourceLocation decl_location = cached_location(fields, 6, location);
            scan.macros.push_back({.name = fields[0],
                                   .arity = std::stoi(fields[1]),
                                   .function_like = fields[2] == "1",
                                   .identity = symbol_id(fields, 3, 4),
                                   .location = decl_location,
                                   .doc_comment = fields[5]});
        } else if (tag == "NN" && fields.size() == 7) {
            scan.namespaces.push_back({.name = fields[0],
                                       .identity = symbol_id(fields, 1, 2),
                                       .location = cached_location(fields, 4, location),
                                       .doc_comment = fields[3]});
        } else if (tag == "CLS" && fields.size() == 16) {
            const SourceLocation decl_location = cached_location(fields, 11, location);
            ClassDecl klass;
            klass.name = fields[0];
            klass.cpp_name = fields[1];
            klass.identity = symbol_id(fields, 2, 3);
            klass.layout = cached_layout(fields, 14);
            klass.native_declaration = true;
            klass.generic_params = native_cache_split_strings(fields[4]);
            if (!fields[5].empty()) {
                klass.generic_min_args = static_cast<size_t>(std::stoull(fields[5]));
            }
            klass.generic_default_args = cached_type_refs(fields[6], decl_location);
            klass.native_specialization_args = cached_type_refs(fields[7], decl_location);
            klass.native_partial_specialization = fields[8] == "1";
            klass.origin_module = fields[9];
            klass.doc_comment = fields[10];
            klass.location = decl_location;
            scan.classes.push_back(std::move(klass));
            current_class = &scan.classes.back();
        } else if (tag == "BASE" && fields.size() == 4 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 1, location);
            current_class->base_class_refs.push_back(
                {.type_ref = cached_type_ref(fields[0], decl_location), .location = decl_location});
        } else if (tag == "TAL" && fields.size() == 8 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 5, location);
            current_class->type_aliases.push_back(
                {.name = fields[0],
                 .cpp_name = fields[1],
                 .type_ref = cached_type_ref(fields[2], decl_location),
                 .origin_module = fields[3],
                 .location = decl_location,
                 .doc_comment = fields[4]});
        } else if (tag == "FLD" && fields.size() == 6 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 3, location);
            current_class->fields.push_back({.name = fields[0],
                                             .type_ref = cached_type_ref(fields[1], decl_location),
                                             .value_expr = {},
                                             .location = decl_location,
                                             .doc_comment = fields[2]});
        } else if (tag == "SFLD" && fields.size() == 6 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 3, location);
            current_class->static_fields.push_back(
                {.name = fields[0],
                 .cpp_name = {},
                 .type_ref = cached_type_ref(fields[1], decl_location),
                 .value_expr = {},
                 .origin_module = {},
                 .location = decl_location,
                 .doc_comment = fields[2]});
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
        std::vector<std::string> fields = {
            item.name,         item.native_spelling,         cached_type_text(item.type_ref),
            item.identity.usr, item.identity.canonical_path, item.doc_comment};
        append_location_fields(fields, item.location);
        fields.push_back(native_cache_join_strings(item.generic_params));
        fields.push_back(item.generic_min_args ? std::to_string(*item.generic_min_args) : "");
        fields.push_back(native_cache_join_strings(cached_type_texts(item.generic_default_args)));
        append_layout_fields(fields, item.layout);
        write_record(out, "NT", fields);
    }
    for (const NativeValueDecl& item : scan.values) {
        std::vector<std::string> fields = {item.name,
                                           item.native_spelling,
                                           cached_type_text(item.type_ref),
                                           item.enum_constant ? "1" : "0",
                                           item.identity.usr,
                                           item.identity.canonical_path,
                                           item.doc_comment};
        append_location_fields(fields, item.location);
        write_record(out, "NV", fields);
    }
    for (const NativeFunctionDecl& item : scan.functions) {
        std::vector<std::string> fields = {
            item.name,
            native_cache_join_strings(item.template_params),
            native_cache_join_strings(item.param_names),
            native_cache_join_strings(item.param_native_spellings),
            native_cache_join_strings(cached_type_texts(item.param_type_refs)),
            item.return_native_spelling,
            cached_type_text(item.return_type_ref),
            std::to_string(item.min_params),
            item.variadic ? "1" : "0",
            item.identity.usr,
            item.identity.canonical_path,
            item.doc_comment};
        append_location_fields(fields, item.location);
        write_record(out, "NF", fields);
    }
    for (const NativeMacroDecl& item : scan.macros) {
        std::vector<std::string> fields = {
            item.name,         std::to_string(item.arity),   item.function_like ? "1" : "0",
            item.identity.usr, item.identity.canonical_path, item.doc_comment};
        append_location_fields(fields, item.location);
        write_record(out, "NM", fields);
    }
    for (const NativeNamespaceDecl& item : scan.namespaces) {
        std::vector<std::string> fields = {item.name, item.identity.usr,
                                           item.identity.canonical_path, item.doc_comment};
        append_location_fields(fields, item.location);
        write_record(out, "NN", fields);
    }
    for (const ClassDecl& klass : scan.classes) {
        std::vector<std::string> fields = {
            klass.name,
            klass.cpp_name,
            klass.identity.usr,
            klass.identity.canonical_path,
            native_cache_join_strings(klass.generic_params),
            klass.generic_min_args ? std::to_string(*klass.generic_min_args) : "",
            native_cache_join_strings(cached_type_texts(klass.generic_default_args)),
            native_cache_join_strings(cached_type_texts(klass.native_specialization_args)),
            klass.native_partial_specialization ? "1" : "0",
            klass.origin_module,
            klass.doc_comment};
        append_location_fields(fields, klass.location);
        append_layout_fields(fields, klass.layout);
        write_record(out, "CLS", fields);
        for (const BaseClassDecl& base : klass.base_class_refs) {
            std::vector<std::string> base_fields = {cached_type_text(base.type_ref)};
            append_location_fields(base_fields, base.location);
            write_record(out, "BASE", base_fields);
        }
        for (const TypeAliasDecl& alias : klass.type_aliases) {
            std::vector<std::string> alias_fields = {alias.name, alias.cpp_name,
                                                     cached_type_text(alias.type_ref),
                                                     alias.origin_module, alias.doc_comment};
            append_location_fields(alias_fields, alias.location);
            write_record(out, "TAL", alias_fields);
        }
        for (const FieldDecl& field : klass.fields) {
            std::vector<std::string> field_fields = {field.name, cached_type_text(field.type_ref),
                                                     field.doc_comment};
            append_location_fields(field_fields, field.location);
            write_record(out, "FLD", field_fields);
        }
        for (const ConstDecl& field : klass.static_fields) {
            std::vector<std::string> field_fields = {field.name, cached_type_text(field.type_ref),
                                                     field.doc_comment};
            append_location_fields(field_fields, field.location);
            write_record(out, "SFLD", field_fields);
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
