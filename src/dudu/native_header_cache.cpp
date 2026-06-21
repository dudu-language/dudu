#include "dudu/native_header_cache.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/native_header_cache_deps.hpp"
#include "dudu/project_config.hpp"

#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

constexpr std::string_view kScanCacheVersion = "dudu-native-scan-v4";
constexpr char kListSeparator = '\x1f';

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
        return project_path(options.config, options.config.build_dir) / "dudu-header-cache";
    }
    return std::filesystem::temp_directory_path() / "dudu-header-cache";
}

std::string cache_id(const std::string& key) {
    return std::to_string(std::hash<std::string>{}(key));
}

std::string join_strings(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out.push_back(kListSeparator);
        }
        out += values[i];
    }
    return out;
}

std::vector<std::string> split_strings(const std::string& text) {
    std::vector<std::string> out;
    if (text.empty()) {
        return out;
    }
    size_t start = 0;
    while (start <= text.size()) {
        const size_t next = text.find(kListSeparator, start);
        out.push_back(
            text.substr(start, next == std::string::npos ? std::string::npos : next - start));
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }
    return out;
}

void append_number(std::string& out, const size_t value) {
    out += std::to_string(value);
    out.push_back(';');
}

void append_string(std::string& out, const std::string& value) {
    out += std::to_string(value.size());
    out.push_back(':');
    out += value;
}

void append_type_ref(std::string& out, const TypeRef& type) {
    append_number(out, static_cast<size_t>(type.kind));
    append_number(out, type.malformed ? 1 : 0);
    append_string(out, type.name);
    append_string(out, type.value);
    append_number(out, type.children.size());
    for (const TypeRef& child : type.children) {
        append_type_ref(out, child);
    }
}

std::optional<size_t> read_number(std::string_view text, size_t& offset) {
    const size_t end = text.find(';', offset);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const size_t value =
        static_cast<size_t>(std::stoull(std::string(text.substr(offset, end - offset))));
    offset = end + 1;
    return value;
}

std::optional<std::string> read_string(std::string_view text, size_t& offset) {
    const size_t colon = text.find(':', offset);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const size_t size =
        static_cast<size_t>(std::stoull(std::string(text.substr(offset, colon - offset))));
    const size_t data = colon + 1;
    if (data + size > text.size()) {
        return std::nullopt;
    }
    offset = data + size;
    return std::string(text.substr(data, size));
}

std::optional<TypeRef> read_type_ref(std::string_view text, size_t& offset,
                                     const SourceLocation& location, const bool root) {
    const std::optional<size_t> kind = read_number(text, offset);
    const std::optional<size_t> malformed = read_number(text, offset);
    const std::optional<std::string> name = read_string(text, offset);
    const std::optional<std::string> value = read_string(text, offset);
    const std::optional<size_t> child_count = read_number(text, offset);
    if (!kind || !malformed || !name || !value || !child_count) {
        return std::nullopt;
    }
    TypeRef type;
    type.kind = static_cast<TypeKind>(*kind);
    type.malformed = *malformed != 0;
    type.name = *name;
    type.value = *value;
    if (root) {
        type.location = location;
        type.range.start = location;
        type.range.end = location;
    }
    for (size_t i = 0; i < *child_count; ++i) {
        const std::optional<TypeRef> child = read_type_ref(text, offset, location, false);
        if (!child) {
            return std::nullopt;
        }
        type.children.push_back(*child);
    }
    return type;
}

std::string cached_type_text(const TypeRef& type) {
    std::string out;
    append_type_ref(out, type);
    return out;
}

TypeRef cached_type_ref(const std::string& text, const SourceLocation& location) {
    size_t offset = 0;
    const std::optional<TypeRef> type = read_type_ref(text, offset, location, true);
    if (!type || offset != text.size()) {
        return parse_type_text(text, location);
    }
    return *type;
}

std::vector<std::string> cached_type_texts(const std::vector<TypeRef>& types) {
    std::vector<std::string> out;
    out.reserve(types.size());
    for (const TypeRef& type : types) {
        out.push_back(cached_type_text(type));
    }
    return out;
}

std::vector<TypeRef> cached_type_refs(const std::string& text, const SourceLocation& location) {
    std::vector<TypeRef> out;
    for (const std::string& item : split_strings(text)) {
        out.push_back(cached_type_ref(item, location));
    }
    return out;
}

void write_record(std::ostream& out, std::string_view tag, const std::vector<std::string>& fields) {
    out << tag;
    for (const std::string& field : fields) {
        out << '\t' << field.size() << ':' << field;
    }
    out << '\n';
}

std::optional<std::pair<std::string, std::vector<std::string>>>
parse_record(const std::string& line) {
    const size_t first_tab = line.find('\t');
    const std::string tag = line.substr(0, first_tab);
    std::vector<std::string> fields;
    size_t offset = first_tab == std::string::npos ? line.size() : first_tab + 1;
    while (offset < line.size()) {
        const size_t colon = line.find(':', offset);
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        const size_t size = static_cast<size_t>(std::stoull(line.substr(offset, colon - offset)));
        const size_t data = colon + 1;
        if (data + size > line.size()) {
            return std::nullopt;
        }
        fields.push_back(line.substr(data, size));
        offset = data + size;
        if (offset < line.size()) {
            if (line[offset] != '\t') {
                return std::nullopt;
            }
            ++offset;
        }
    }
    return std::make_pair(tag, fields);
}

NativeSymbolId symbol_id(const std::vector<std::string>& fields, size_t usr_index,
                         size_t path_index) {
    return {.usr = fields[usr_index], .canonical_path = fields[path_index]};
}

std::vector<std::string> location_fields(const SourceLocation& location) {
    return {location.file.string(), std::to_string(location.line), std::to_string(location.column)};
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
    return {.file = fields[index],
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
                                       join_strings(fn.generic_params),
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
    fn.generic_params = split_strings(fields[4]);
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
                                   const std::string& macro_dump, const std::string& dependencies) {
    std::filesystem::create_directories(cache.base.parent_path());
    write_text(cache.base.string() + ".ast", ast_dump);
    write_text(cache.base.string() + ".macros", macro_dump);
    write_text(cache.base.string() + ".deps",
               native_header_dependency_stamps_from_makefile(dependencies));
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
            scan.functions.push_back({.name = fields[0],
                                      .template_params = split_strings(fields[1]),
                                      .param_native_spellings = split_strings(fields[2]),
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
            klass.generic_params = split_strings(fields[4]);
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
        std::vector<std::string> fields = {item.name,
                                           join_strings(item.template_params),
                                           join_strings(item.param_native_spellings),
                                           join_strings(cached_type_texts(item.param_type_refs)),
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
                                           join_strings(klass.generic_params),
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
