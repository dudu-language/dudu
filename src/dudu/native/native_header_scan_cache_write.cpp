#include "dudu/native/native_header_cache.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/native/native_header_cache_format.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

void append_location_fields(std::vector<std::string>& fields, const SourceLocation& location) {
    fields.push_back(location.file.str());
    fields.push_back(std::to_string(location.line));
    fields.push_back(std::to_string(location.column));
}

void append_layout_fields(std::vector<std::string>& fields,
                          const std::optional<TypeLayout>& layout) {
    fields.push_back(layout ? std::to_string(layout->size) : "");
    fields.push_back(layout ? std::to_string(layout->alignment) : "");
}

std::string cache_bool_vector(const std::vector<bool>& values) {
    std::string out;
    out.reserve(values.size());
    for (const bool value : values) {
        out.push_back(value ? '1' : '0');
    }
    return out;
}

std::string cache_native_parameters(const std::vector<NativeParameterMetadata>& parameters) {
    std::vector<std::string> fields;
    fields.reserve(parameters.size() * 3);
    for (const NativeParameterMetadata& parameter : parameters) {
        fields.push_back(parameter.name);
        fields.push_back(parameter.default_value);
        fields.push_back(parameter.doc_comment);
    }
    return native_cache_join_strings(fields);
}

void append_native_metadata_fields(std::vector<std::string>& fields,
                                   const NativeDeclarationMetadata& metadata) {
    fields.push_back(metadata.declaration);
    fields.push_back(metadata.summary_doc_comment);
    fields.push_back(metadata.return_doc_comment);
    fields.push_back(metadata.deprecated_message);
    fields.push_back(cache_native_parameters(metadata.parameters));
    fields.push_back(cache_native_parameters(metadata.template_parameters));
}

void write_function_record(std::ostream& out, std::string_view tag, const FunctionDecl& fn) {
    std::vector<std::string> fields = {
        fn.name,
        fn.cpp_name,
        fn.native_identity.usr,
        fn.native_identity.canonical_path,
        native_cache_join_strings(fn.generic_params),
        cache_bool_vector(fn.generic_param_is_value),
        native_cache_join_strings(cached_type_texts(fn.generic_default_args)),
        cached_type_text(fn.receiver_type_ref),
        cached_type_text(fn.return_type_ref),
        fn.deleted ? "1" : "0",
        std::to_string(fn.min_params),
        fn.doc_comment};
    append_native_metadata_fields(fields, fn.native_metadata);
    append_location_fields(fields, fn.location);
    fields.push_back(std::to_string(fn.params.size()));
    for (const ParamDecl& param : fn.params) {
        fields.push_back(param.name);
        fields.push_back(cached_type_text(param.type_ref));
    }
    write_record(out, tag, fields);
}

} // namespace

void store_native_header_scan_cache(const NativeHeaderRawCache& cache,
                                    const NativeHeaderScan& scan) {
    std::ostringstream out;
    out << kNativeHeaderScanCacheVersion << '\n';
    for (const NativeTypeDecl& item : scan.types) {
        std::vector<std::string> fields = {item.name,
                                           item.native_spelling,
                                           cached_type_text(item.type_ref),
                                           item.enum_type ? "1" : "0",
                                           item.identity.usr,
                                           item.identity.canonical_path,
                                           item.doc_comment};
        append_location_fields(fields, item.location);
        fields.push_back(native_cache_join_strings(item.generic_params));
        fields.push_back(item.generic_min_args ? std::to_string(*item.generic_min_args) : "");
        fields.push_back(native_cache_join_strings(cached_type_texts(item.generic_default_args)));
        append_layout_fields(fields, item.layout);
        append_native_metadata_fields(fields, item.native_metadata);
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
        append_native_metadata_fields(fields, item.native_metadata);
        write_record(out, "NV", fields);
    }
    for (const NativeFunctionDecl& item : scan.functions) {
        std::vector<std::string> fields = {
            item.name,
            native_cache_join_strings(item.template_params),
            cache_bool_vector(item.template_param_is_value),
            native_cache_join_strings(cached_type_texts(item.template_default_args)),
            native_cache_join_strings(item.param_names),
            native_cache_join_strings(item.param_native_spellings),
            native_cache_join_strings(cached_type_texts(item.param_type_refs)),
            item.return_native_spelling,
            cached_type_text(item.return_type_ref),
            std::to_string(item.min_params),
            item.variadic ? "1" : "0",
            item.deleted ? "1" : "0",
            item.identity.usr,
            item.identity.canonical_path,
            item.doc_comment};
        append_native_metadata_fields(fields, item.native_metadata);
        append_location_fields(fields, item.location);
        write_record(out, "NF", fields);
    }
    for (const NativeMacroDecl& item : scan.macros) {
        std::vector<std::string> fields = {item.name,
                                           std::to_string(item.arity),
                                           item.function_like ? "1" : "0",
                                           item.identity.usr,
                                           item.identity.canonical_path,
                                           item.doc_comment,
                                           native_cache_join_strings(item.param_names)};
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
            native_cache_join_strings(cached_type_texts(klass.native_specialization_requirements)),
            klass.native_partial_specialization ? "1" : "0",
            klass.origin_module,
            klass.doc_comment};
        append_location_fields(fields, klass.location);
        append_layout_fields(fields, klass.layout);
        append_native_metadata_fields(fields, klass.native_metadata);
        write_record(out, "CLS", fields);
        for (const BaseClassDecl& base : klass.base_class_refs) {
            std::vector<std::string> base_fields = {cached_type_text(base.type_ref)};
            append_location_fields(base_fields, base.location);
            write_record(out, "BASE", base_fields);
        }
        for (const TypeAliasDecl& alias : klass.type_aliases) {
            std::vector<std::string> alias_fields = {
                alias.name,
                alias.cpp_name,
                cached_type_text(alias.type_ref),
                native_cache_join_strings(alias.generic_params),
                alias.generic_min_args ? std::to_string(*alias.generic_min_args) : "",
                native_cache_join_strings(cached_type_texts(alias.generic_default_args)),
                alias.origin_module,
                alias.doc_comment};
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
                                                     display_expr(field.value_expr),
                                                     field.doc_comment};
            append_location_fields(field_fields, field.location);
            write_record(out, "SFLD", field_fields);
        }
        for (const FunctionDecl& method : klass.methods) {
            write_function_record(out, "MET", method);
        }
        for (const EnumDecl& en : klass.enums) {
            std::vector<std::string> enum_fields = {en.name, en.doc_comment};
            append_location_fields(enum_fields, en.location);
            write_record(out, "NEN", enum_fields);
            for (const EnumValueDecl& value : en.values) {
                std::vector<std::string> value_fields = {value.name, value.doc_comment};
                append_location_fields(value_fields, value.location);
                write_record(out, "NEV", value_fields);
            }
        }
        write_record(out, "ENDCLS", {});
    }
    std::filesystem::create_directories(cache.base.parent_path());
    std::ofstream file(cache.base.string() + ".scan");
    if (file) {
        file << out.str();
    }
}

} // namespace dudu
