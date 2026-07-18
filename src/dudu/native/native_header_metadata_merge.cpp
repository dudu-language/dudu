#include "dudu/native/native_header_metadata_merge.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"

#include <algorithm>
#include <map>

namespace dudu {
namespace {

void fill(std::string& target, const std::string& source) {
    if (target.empty()) {
        target = source;
    }
}

void merge_identity(NativeSymbolId& target, const NativeSymbolId& source) {
    fill(target.usr, source.usr);
    fill(target.canonical_path, source.canonical_path);
}

void merge_location(SourceLocation& target, const SourceLocation& source) {
    if (target.line == 0 && source.line != 0) {
        target = source;
    }
}

void merge_parameter_metadata(NativeParameterMetadata& target,
                              const NativeParameterMetadata& source) {
    fill(target.name, source.name);
    fill(target.default_value, source.default_value);
    fill(target.doc_comment, source.doc_comment);
}

void merge_parameter_metadata_list(std::vector<NativeParameterMetadata>& target,
                                   const std::vector<NativeParameterMetadata>& source) {
    target.resize(std::max(target.size(), source.size()));
    for (size_t index = 0; index < source.size(); ++index) {
        merge_parameter_metadata(target[index], source[index]);
    }
}

bool equivalent_method(const FunctionDecl& left, const FunctionDecl& right) {
    if (left.name != right.name || left.deleted != right.deleted ||
        left.generic_params != right.generic_params ||
        left.generic_param_is_value != right.generic_param_is_value ||
        left.generic_default_args.size() != right.generic_default_args.size() ||
        !type_ref_equivalent(left.receiver_type_ref, right.receiver_type_ref) ||
        !type_ref_equivalent(function_return_type_ref(left), function_return_type_ref(right)) ||
        left.params.size() != right.params.size()) {
        return false;
    }
    for (size_t index = 0; index < left.generic_default_args.size(); ++index) {
        if (!type_ref_equivalent(left.generic_default_args[index],
                                 right.generic_default_args[index])) {
            return false;
        }
    }
    for (size_t index = 0; index < left.params.size(); ++index) {
        if (!type_ref_equivalent(left.params[index].type_ref, right.params[index].type_ref)) {
            return false;
        }
    }
    return true;
}

void merge_field(FieldDecl& target, const FieldDecl& source) {
    if (!has_type_ref(target.type_ref) && has_type_ref(source.type_ref)) {
        target.type_ref = source.type_ref;
    }
    if (expr_missing(target.value_expr) && expr_present(source.value_expr)) {
        target.value_expr = source.value_expr;
    }
    fill(target.doc_comment, source.doc_comment);
    merge_location(target.location, source.location);
}

void merge_constant(ConstDecl& target, const ConstDecl& source) {
    fill(target.cpp_name, source.cpp_name);
    if (!has_type_ref(target.type_ref) && has_type_ref(source.type_ref)) {
        target.type_ref = source.type_ref;
    }
    if (expr_missing(target.value_expr) && expr_present(source.value_expr)) {
        target.value_expr = source.value_expr;
    }
    fill(target.origin_module, source.origin_module);
    fill(target.doc_comment, source.doc_comment);
    merge_location(target.location, source.location);
}

void merge_alias(TypeAliasDecl& target, const TypeAliasDecl& source) {
    fill(target.cpp_name, source.cpp_name);
    if (!has_type_ref(target.type_ref) && has_type_ref(source.type_ref)) {
        target.type_ref = source.type_ref;
    }
    if (target.generic_params.empty()) {
        target.generic_params = source.generic_params;
    }
    if (!target.generic_min_args && source.generic_min_args) {
        target.generic_min_args = source.generic_min_args;
    }
    if (target.generic_default_args.empty()) {
        target.generic_default_args = source.generic_default_args;
    }
    fill(target.origin_module, source.origin_module);
    fill(target.doc_comment, source.doc_comment);
    merge_location(target.location, source.location);
}

void merge_method(FunctionDecl& target, const FunctionDecl& source) {
    fill(target.cpp_name, source.cpp_name);
    merge_identity(target.native_identity, source.native_identity);
    fill(target.origin_module, source.origin_module);
    fill(target.doc_comment, source.doc_comment);
    merge_location(target.location, source.location);
    merge_native_declaration_metadata(target.native_metadata, source.native_metadata);
    for (size_t index = 0; index < target.params.size() && index < source.params.size(); ++index) {
        fill(target.params[index].name, source.params[index].name);
        merge_location(target.params[index].location, source.params[index].location);
    }
}

void merge_enum_value(EnumValueDecl& target, const EnumValueDecl& source) {
    if (expr_missing(target.value_expr) && expr_present(source.value_expr)) {
        target.value_expr = source.value_expr;
    }
    fill(target.doc_comment, source.doc_comment);
    merge_location(target.location, source.location);
    if (target.payload_fields.empty()) {
        target.payload_fields = source.payload_fields;
    }
}

void merge_enum(EnumDecl& target, const EnumDecl& source) {
    fill(target.cpp_name, source.cpp_name);
    if (!has_type_ref(target.underlying_type_ref) && has_type_ref(source.underlying_type_ref)) {
        target.underlying_type_ref = source.underlying_type_ref;
    }
    fill(target.origin_module, source.origin_module);
    fill(target.doc_comment, source.doc_comment);
    merge_location(target.location, source.location);

    std::map<std::string, size_t> values;
    for (size_t index = 0; index < target.values.size(); ++index) {
        values.emplace(target.values[index].name, index);
    }
    for (const EnumValueDecl& value : source.values) {
        const auto existing = values.find(value.name);
        if (existing == values.end()) {
            values.emplace(value.name, target.values.size());
            target.values.push_back(value);
        } else {
            merge_enum_value(target.values[existing->second], value);
        }
    }
    for (const FunctionDecl& method : source.methods) {
        const auto existing = std::ranges::find_if(target.methods, [&](const FunctionDecl& item) {
            return equivalent_method(item, method);
        });
        if (existing == target.methods.end()) {
            target.methods.push_back(method);
        } else {
            merge_method(*existing, method);
        }
    }
}

template <typename T, typename Merge>
void merge_named_members(std::vector<T>& target, const std::vector<T>& source, Merge merge) {
    std::map<std::string, size_t> members;
    for (size_t index = 0; index < target.size(); ++index) {
        members.emplace(target[index].name, index);
    }
    for (const T& item : source) {
        const auto existing = members.find(item.name);
        if (existing == members.end()) {
            members.emplace(item.name, target.size());
            target.push_back(item);
        } else {
            merge(target[existing->second], item);
        }
    }
}

} // namespace

void merge_native_declaration_metadata(NativeDeclarationMetadata& target,
                                       const NativeDeclarationMetadata& source) {
    fill(target.declaration, source.declaration);
    fill(target.summary_doc_comment, source.summary_doc_comment);
    fill(target.return_doc_comment, source.return_doc_comment);
    fill(target.deprecated_message, source.deprecated_message);
    merge_parameter_metadata_list(target.parameters, source.parameters);
    merge_parameter_metadata_list(target.template_parameters, source.template_parameters);
}

void merge_native_value_declaration(NativeValueDecl& target,
                                    const NativeValueDecl& source) {
    fill(target.native_spelling, source.native_spelling);
    if (!has_type_ref(target.type_ref) && has_type_ref(source.type_ref)) {
        target.type_ref = source.type_ref;
    }
    target.enum_constant = target.enum_constant || source.enum_constant;
    merge_identity(target.identity, source.identity);
    merge_location(target.location, source.location);
    fill(target.doc_comment, source.doc_comment);
    merge_native_declaration_metadata(target.native_metadata, source.native_metadata);
}

void merge_native_function_declaration(NativeFunctionDecl& target,
                                       const NativeFunctionDecl& source) {
    merge_identity(target.identity, source.identity);
    merge_location(target.location, source.location);
    fill(target.doc_comment, source.doc_comment);
    fill(target.return_native_spelling, source.return_native_spelling);
    if (!has_type_ref(target.return_type_ref) && has_type_ref(source.return_type_ref)) {
        target.return_type_ref = source.return_type_ref;
    }
    target.param_names.resize(std::max(target.param_names.size(), source.param_names.size()));
    for (size_t index = 0; index < source.param_names.size(); ++index) {
        fill(target.param_names[index], source.param_names[index]);
    }
    target.param_native_spellings.resize(
        std::max(target.param_native_spellings.size(), source.param_native_spellings.size()));
    for (size_t index = 0; index < source.param_native_spellings.size(); ++index) {
        fill(target.param_native_spellings[index], source.param_native_spellings[index]);
    }
    merge_native_declaration_metadata(target.native_metadata, source.native_metadata);
}

void merge_native_macro_declaration(NativeMacroDecl& target,
                                    const NativeMacroDecl& source) {
    if (target.arity < 0) {
        target.arity = source.arity;
    }
    target.function_like = target.function_like || source.function_like;
    if (target.param_names.empty()) {
        target.param_names = source.param_names;
    }
    merge_identity(target.identity, source.identity);
    merge_location(target.location, source.location);
    fill(target.doc_comment, source.doc_comment);
}

void merge_native_namespace_declaration(NativeNamespaceDecl& target,
                                        const NativeNamespaceDecl& source) {
    merge_identity(target.identity, source.identity);
    merge_location(target.location, source.location);
    fill(target.doc_comment, source.doc_comment);
}

void merge_native_class_members(ClassDecl& target, const ClassDecl& source) {
    for (const BaseClassDecl& base : source.base_class_refs) {
        const bool exists = std::ranges::any_of(target.base_class_refs, [&](const auto& item) {
            return type_ref_equivalent(item.type_ref, base.type_ref);
        });
        if (!exists) {
            target.base_class_refs.push_back(base);
        }
    }
    merge_named_members(target.fields, source.fields, merge_field);
    merge_named_members(target.constants, source.constants, merge_constant);
    merge_named_members(target.static_fields, source.static_fields, merge_constant);
    merge_named_members(target.type_aliases, source.type_aliases, merge_alias);
    merge_named_members(target.enums, source.enums, merge_enum);

    for (const FunctionDecl& method : source.methods) {
        const auto existing = std::ranges::find_if(target.methods, [&](const FunctionDecl& item) {
            return equivalent_method(item, method);
        });
        if (existing == target.methods.end()) {
            target.methods.push_back(method);
        } else {
            merge_method(*existing, method);
        }
    }
}

} // namespace dudu
