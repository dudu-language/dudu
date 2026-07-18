#include "dudu/native/native_header_cache.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/file_io.hpp"
#include "dudu/native/native_header_cache_format.hpp"

#include <optional>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

NativeSymbolId symbol_id(const std::vector<std::string>& fields, size_t usr_index,
                         size_t path_index) {
    return {.usr = fields[usr_index], .canonical_path = fields[path_index]};
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

std::vector<bool> cached_bool_vector(std::string_view text) {
    std::vector<bool> out;
    out.reserve(text.size());
    for (const char value : text) {
        if (value != '0' && value != '1') {
            return {};
        }
        out.push_back(value == '1');
    }
    return out;
}

std::vector<NativeParameterMetadata> cached_native_parameters(std::string_view text) {
    const std::vector<std::string> fields = native_cache_split_strings(std::string(text));
    if (fields.size() % 3 != 0) {
        return {};
    }
    std::vector<NativeParameterMetadata> out;
    out.reserve(fields.size() / 3);
    for (size_t index = 0; index < fields.size(); index += 3) {
        out.push_back({.name = fields[index],
                       .default_value = fields[index + 1],
                       .doc_comment = fields[index + 2]});
    }
    return out;
}

NativeDeclarationMetadata cached_native_metadata(const std::vector<std::string>& fields,
                                                 size_t index) {
    return {.declaration = fields[index],
            .summary_doc_comment = fields[index + 1],
            .return_doc_comment = fields[index + 2],
            .deprecated_message = fields[index + 3],
            .parameters = cached_native_parameters(fields[index + 4]),
            .template_parameters = cached_native_parameters(fields[index + 5])};
}

ParamDecl cached_param(std::string name, const std::string& type, const SourceLocation& location) {
    return {
        .name = std::move(name), .type_ref = cached_type_ref(type, location), .location = location};
}

std::optional<FunctionDecl> read_function_record(const std::vector<std::string>& fields,
                                                 const SourceLocation& location) {
    if (fields.size() < 22) {
        return std::nullopt;
    }
    const SourceLocation function_location = cached_location(fields, 18, location);
    FunctionDecl fn;
    fn.name = fields[0];
    fn.cpp_name = fields[1];
    fn.native_identity = symbol_id(fields, 2, 3);
    fn.generic_params = native_cache_split_strings(fields[4]);
    fn.generic_param_is_value = cached_bool_vector(fields[5]);
    fn.generic_default_args = cached_type_refs(fields[6], function_location);
    fn.receiver_type_ref = cached_type_ref(fields[7], function_location);
    fn.return_type_ref = cached_type_ref(fields[8], function_location);
    fn.deleted = fields[9] == "1";
    fn.min_params = std::stoi(fields[10]);
    fn.doc_comment = fields[11];
    fn.native_metadata = cached_native_metadata(fields, 12);
    fn.location = function_location;
    const size_t param_count = static_cast<size_t>(std::stoull(fields[21]));
    if (fields.size() != 22 + param_count * 2) {
        return std::nullopt;
    }
    for (size_t i = 0; i < param_count; ++i) {
        fn.params.push_back(
            cached_param(fields[22 + i * 2], fields[23 + i * 2], function_location));
    }
    return fn;
}

} // namespace

std::optional<NativeHeaderScan> load_native_header_scan_cache(const NativeHeaderRawCache& cache,
                                                              const SourceLocation& location) {
    if (!cache.hit) {
        return std::nullopt;
    }
    const std::string text =
        try_read_text_file(cache.base.string() + ".scan").value_or("");
    if (text.empty()) {
        return std::nullopt;
    }
    std::istringstream in(text);
    std::string version;
    if (!std::getline(in, version) || version != kNativeHeaderScanCacheVersion) {
        return std::nullopt;
    }
    NativeHeaderScan scan;
    ClassDecl* current_class = nullptr;
    while (in.peek() != std::char_traits<char>::eof()) {
        const auto parsed = read_record(in);
        if (!parsed) {
            return std::nullopt;
        }
        const std::string& tag = parsed->first;
        const std::vector<std::string>& fields = parsed->second;
        if (tag == "NT" && fields.size() == 21) {
            const SourceLocation decl_location = cached_location(fields, 7, location);
            NativeTypeDecl type{.name = fields[0],
                                .native_spelling = fields[1],
                                .type_ref = cached_type_ref(fields[2], decl_location),
                                .enum_type = fields[3] == "1",
                                .identity = symbol_id(fields, 4, 5),
                                .layout = cached_layout(fields, 13),
                                .location = decl_location,
                                .doc_comment = fields[6]};
            type.generic_params = native_cache_split_strings(fields[10]);
            if (!fields[11].empty()) {
                type.generic_min_args = static_cast<size_t>(std::stoull(fields[11]));
            }
            type.generic_default_args = cached_type_refs(fields[12], decl_location);
            type.native_metadata = cached_native_metadata(fields, 15);
            scan.types.push_back(std::move(type));
        } else if (tag == "NV" && fields.size() == 16) {
            const SourceLocation decl_location = cached_location(fields, 7, location);
            scan.values.push_back({.name = fields[0],
                                   .native_spelling = fields[1],
                                   .type_ref = cached_type_ref(fields[2], decl_location),
                                   .enum_constant = fields[3] == "1",
                                   .identity = symbol_id(fields, 4, 5),
                                   .location = decl_location,
                                   .doc_comment = fields[6],
                                   .native_metadata = cached_native_metadata(fields, 10)});
        } else if (tag == "NF" && fields.size() == 24) {
            const SourceLocation decl_location = cached_location(fields, 21, location);
            scan.functions.push_back(
                {.name = fields[0],
                 .template_params = native_cache_split_strings(fields[1]),
                 .template_param_is_value = cached_bool_vector(fields[2]),
                 .template_default_args = cached_type_refs(fields[3], decl_location),
                 .param_names = native_cache_split_strings(fields[4]),
                 .param_native_spellings = native_cache_split_strings(fields[5]),
                 .param_type_refs = cached_type_refs(fields[6], decl_location),
                 .return_native_spelling = fields[7],
                 .return_type_ref = cached_type_ref(fields[8], decl_location),
                 .min_params = std::stoi(fields[9]),
                 .variadic = fields[10] == "1",
                 .deleted = fields[11] == "1",
                 .identity = symbol_id(fields, 12, 13),
                 .location = decl_location,
                 .doc_comment = fields[14],
                 .native_metadata = cached_native_metadata(fields, 15)});
        } else if (tag == "NM" && fields.size() == 10) {
            const SourceLocation decl_location = cached_location(fields, 7, location);
            scan.macros.push_back({.name = fields[0],
                                   .arity = std::stoi(fields[1]),
                                   .function_like = fields[2] == "1",
                                   .param_names = native_cache_split_strings(fields[6]),
                                   .identity = symbol_id(fields, 3, 4),
                                   .location = decl_location,
                                   .doc_comment = fields[5]});
        } else if (tag == "NN" && fields.size() == 7) {
            scan.namespaces.push_back({.name = fields[0],
                                       .identity = symbol_id(fields, 1, 2),
                                       .location = cached_location(fields, 4, location),
                                       .doc_comment = fields[3]});
        } else if (tag == "CLS" && fields.size() == 23) {
            const SourceLocation decl_location = cached_location(fields, 12, location);
            ClassDecl klass;
            klass.name = fields[0];
            klass.cpp_name = fields[1];
            klass.identity = symbol_id(fields, 2, 3);
            klass.layout = cached_layout(fields, 15);
            klass.native_declaration = true;
            klass.generic_params = native_cache_split_strings(fields[4]);
            if (!fields[5].empty()) {
                klass.generic_min_args = static_cast<size_t>(std::stoull(fields[5]));
            }
            klass.generic_default_args = cached_type_refs(fields[6], decl_location);
            klass.native_specialization_args = cached_type_refs(fields[7], decl_location);
            klass.native_specialization_requirements = cached_type_refs(fields[8], decl_location);
            klass.native_partial_specialization = fields[9] == "1";
            klass.origin_module = fields[10];
            klass.doc_comment = fields[11];
            klass.location = decl_location;
            klass.native_metadata = cached_native_metadata(fields, 17);
            scan.classes.push_back(std::move(klass));
            current_class = &scan.classes.back();
        } else if (tag == "BASE" && fields.size() == 4 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 1, location);
            current_class->base_class_refs.push_back(
                {.type_ref = cached_type_ref(fields[0], decl_location), .location = decl_location});
        } else if (tag == "TAL" && fields.size() == 11 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 8, location);
            current_class->type_aliases.push_back(
                {.name = fields[0],
                 .cpp_name = fields[1],
                 .type_ref = cached_type_ref(fields[2], decl_location),
                 .generic_params = native_cache_split_strings(fields[3]),
                 .generic_min_args = fields[4].empty() ? std::nullopt
                                                       : std::optional<size_t>{static_cast<size_t>(
                                                             std::stoull(fields[4]))},
                 .generic_default_args = cached_type_refs(fields[5], decl_location),
                 .origin_module = fields[6],
                 .location = decl_location,
                 .doc_comment = fields[7]});
        } else if (tag == "FLD" && fields.size() == 6 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 3, location);
            current_class->fields.push_back({.name = fields[0],
                                             .type_ref = cached_type_ref(fields[1], decl_location),
                                             .value_expr = {},
                                             .decorators = {},
                                             .location = decl_location,
                                             .doc_comment = fields[2]});
        } else if (tag == "SFLD" && fields.size() == 7 && current_class != nullptr) {
            const SourceLocation decl_location = cached_location(fields, 4, location);
            current_class->static_fields.push_back(
                {.name = fields[0],
                 .cpp_name = {},
                 .type_ref = cached_type_ref(fields[1], decl_location),
                 .value_expr = parse_expr_text(fields[2], decl_location),
                 .decorators = {},
                 .origin_module = {},
                 .location = decl_location,
                 .doc_comment = fields[3]});
        } else if (tag == "MET" && current_class != nullptr) {
            const std::optional<FunctionDecl> fn = read_function_record(fields, location);
            if (!fn) {
                return std::nullopt;
            }
            current_class->methods.push_back(*fn);
        } else if (tag == "NEN" && fields.size() == 5 && current_class != nullptr) {
            current_class->enums.push_back({.name = fields[0],
                                            .cpp_name = {},
                                            .underlying_type_ref = {},
                                            .origin_module = {},
                                            .values = {},
                                            .methods = {},
                                            .decorators = {},
                                            .location = cached_location(fields, 2, location),
                                            .range = {},
                                            .doc_comment = fields[1]});
        } else if (tag == "NEV" && fields.size() == 5 && current_class != nullptr &&
                   !current_class->enums.empty()) {
            current_class->enums.back().values.push_back(
                {.name = fields[0],
                 .value_expr = {},
                 .payload_fields = {},
                 .decorators = {},
                 .location = cached_location(fields, 2, location),
                 .doc_comment = fields[1]});
        } else if (tag == "ENDCLS") {
            current_class = nullptr;
        } else {
            return std::nullopt;
        }
    }
    return scan;
}

} // namespace dudu
