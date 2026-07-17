#include "dudu/native/native_header_merge.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/native/native_header_collision.hpp"
#include "dudu/native/native_header_identity.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <type_traits>

namespace dudu {

bool type_ref_lists_equivalent(const std::vector<TypeRef>& lhs,
                               const std::vector<TypeRef>& rhs);

namespace {

template <typename T>
void append_unique_native_decls(std::vector<T>& target, const std::vector<T>& source,
                                std::string_view kind,
                                const std::map<std::string, std::string>& aliases = {}) {
    std::map<std::string, T> seen_by_name;
    for (const T& item : target) {
        seen_by_name[item.name] = item;
    }
    for (const T& item : source) {
        const std::string identity = native_decl_identity_key(item);
        const auto existing = seen_by_name.find(item.name);
        if (existing == seen_by_name.end()) {
            seen_by_name[item.name] = item;
            target.push_back(item);
            continue;
        }
        bool collision = native_decl_identity_key(existing->second) != identity;
        if constexpr (std::is_same_v<T, NativeTypeDecl>) {
            collision = collision &&
                        !native_type_redeclarations_compatible(existing->second, item, aliases);
        } else if constexpr (std::is_same_v<T, NativeNamespaceDecl>) {
            collision = false;
        }
        if (collision && native_decl_collision_is_error(item.name, item.location)) {
            std::string message = "native " + std::string(kind) + " name collision: " + item.name;
            if constexpr (std::is_same_v<T, NativeTypeDecl>) {
                message += " (existing " + native_decl_identity_key(existing->second) + " as '" +
                           existing->second.native_spelling + "', incoming " + identity + " as '" +
                           item.native_spelling + "')";
            }
            throw CompileError(item.location, std::move(message));
        }
    }
}

bool contains_equivalent_method(const std::vector<FunctionDecl>& methods,
                                const FunctionDecl& candidate) {
    return std::ranges::any_of(methods, [&](const FunctionDecl& method) {
        if (method.name != candidate.name ||
            method.deleted != candidate.deleted ||
            method.generic_params != candidate.generic_params ||
            method.generic_param_is_value != candidate.generic_param_is_value ||
            !type_ref_lists_equivalent(method.generic_default_args,
                                       candidate.generic_default_args) ||
            !type_ref_equivalent(method.receiver_type_ref, candidate.receiver_type_ref) ||
            !type_ref_equivalent(function_return_type_ref(method),
                                 function_return_type_ref(candidate)) ||
            method.params.size() != candidate.params.size()) {
            return false;
        }
        for (size_t i = 0; i < method.params.size(); ++i) {
            if (!type_ref_equivalent(method.params[i].type_ref, candidate.params[i].type_ref)) {
                return false;
            }
        }
        return true;
    });
}

bool has_equivalent_base_class(const ClassDecl& klass, const TypeRef& type) {
    return std::ranges::any_of(klass.base_class_refs, [&](const BaseClassDecl& base) {
        return type_ref_equivalent(base.type_ref, type);
    });
}

bool synthetic_template_param(std::string_view name) {
    if (name.ends_with("...")) {
        name.remove_suffix(3);
    }
    return name.starts_with("__dudu_native_type_parameter_");
}

std::map<std::string, TypeRef>
template_param_renames(const std::vector<std::string>& from,
                       const std::vector<std::string>& to) {
    std::map<std::string, TypeRef> renames;
    if (from.size() != to.size()) {
        return renames;
    }
    for (size_t i = 0; i < from.size(); ++i) {
        if (from[i] != to[i]) {
            renames.emplace(from[i], named_type_ref(to[i]));
        }
    }
    return renames;
}

void rename_type_refs(std::vector<TypeRef>& types,
                      const std::map<std::string, TypeRef>& renames) {
    for (TypeRef& type : types) {
        type = substitute_type_ref(type, renames);
    }
}

void rename_class_template_params(ClassDecl& klass,
                                  const std::vector<std::string>& canonical_params) {
    const std::map<std::string, TypeRef> renames =
        template_param_renames(klass.generic_params, canonical_params);
    if (renames.empty()) {
        klass.generic_params = canonical_params;
        return;
    }

    rename_type_refs(klass.generic_default_args, renames);
    rename_type_refs(klass.native_specialization_args, renames);
    rename_type_refs(klass.native_specialization_requirements, renames);
    for (BaseClassDecl& base : klass.base_class_refs) {
        base.type_ref = substitute_type_ref(base.type_ref, renames);
    }
    for (TypeAliasDecl& alias : klass.type_aliases) {
        alias.type_ref = substitute_type_ref(alias.type_ref, renames);
        rename_type_refs(alias.generic_default_args, renames);
    }
    for (FieldDecl& field : klass.fields) {
        field.type_ref = substitute_type_ref(field.type_ref, renames);
    }
    for (ConstDecl& value : klass.constants) {
        value.type_ref = substitute_type_ref(value.type_ref, renames);
    }
    for (ConstDecl& value : klass.static_fields) {
        value.type_ref = substitute_type_ref(value.type_ref, renames);
    }
    for (FunctionDecl& method : klass.methods) {
        method.receiver_type_ref = substitute_type_ref(method.receiver_type_ref, renames);
        method.return_type_ref = substitute_type_ref(method.return_type_ref, renames);
        rename_type_refs(method.generic_default_args, renames);
        for (ParamDecl& param : method.params) {
            param.type_ref = substitute_type_ref(param.type_ref, renames);
        }
    }
    klass.generic_params = canonical_params;
}

void rename_native_type_template_params(NativeTypeDecl& type,
                                        const std::vector<std::string>& canonical_params) {
    const std::map<std::string, TypeRef> renames =
        template_param_renames(type.generic_params, canonical_params);
    if (!renames.empty()) {
        type.type_ref = substitute_type_ref(type.type_ref, renames);
        rename_type_refs(type.generic_default_args, renames);
    }
    type.generic_params = canonical_params;
}

template <typename T>
std::vector<std::string> canonical_template_params(const T& target, const T& source) {
    if (target.generic_params.size() != source.generic_params.size()) {
        return target.generic_params;
    }
    std::vector<std::string> canonical = target.generic_params;
    for (size_t i = 0; i < canonical.size(); ++i) {
        if (synthetic_template_param(canonical[i]) &&
            !synthetic_template_param(source.generic_params[i])) {
            canonical[i] = source.generic_params[i];
        }
    }
    return canonical;
}

void merge_native_type_declaration_impl(NativeTypeDecl& target, NativeTypeDecl source) {
    if (!target.generic_params.empty() &&
        target.generic_params.size() == source.generic_params.size()) {
        const std::vector<std::string> canonical =
            canonical_template_params(target, source);
        rename_native_type_template_params(target, canonical);
        rename_native_type_template_params(source, canonical);
    }
    if (target.native_spelling.empty()) {
        target.native_spelling = source.native_spelling;
    }
    if (!has_type_ref(target.type_ref) && has_type_ref(source.type_ref)) {
        target.type_ref = source.type_ref;
    }
    target.enum_type = target.enum_type || source.enum_type;
    if (target.identity.usr.empty()) {
        target.identity.usr = source.identity.usr;
    }
    if (target.identity.canonical_path.empty()) {
        target.identity.canonical_path = source.identity.canonical_path;
    }
    if (!target.layout && source.layout) {
        target.layout = source.layout;
    }
    if (target.location.line == 0 && source.location.line != 0) {
        target.location = source.location;
    }
    if (target.doc_comment.empty()) {
        target.doc_comment = source.doc_comment;
    }
    if (target.generic_params.empty()) {
        target.generic_params = source.generic_params;
    }
    if (source.generic_min_args &&
        (!target.generic_min_args || *source.generic_min_args < *target.generic_min_args)) {
        target.generic_min_args = source.generic_min_args;
    }
    if (target.generic_default_args.size() < source.generic_default_args.size()) {
        target.generic_default_args.resize(source.generic_default_args.size());
    }
    for (size_t i = 0; i < source.generic_default_args.size(); ++i) {
        if (!has_type_ref(target.generic_default_args[i]) &&
            has_type_ref(source.generic_default_args[i])) {
            target.generic_default_args[i] = source.generic_default_args[i];
        }
    }
}

} // namespace

void merge_native_type_declaration(NativeTypeDecl& target,
                                   const NativeTypeDecl& source) {
    merge_native_type_declaration_impl(target, source);
}

bool type_ref_lists_equivalent(const std::vector<TypeRef>& lhs, const std::vector<TypeRef>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!type_ref_equivalent(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

bool native_function_equivalent(const NativeFunctionDecl& lhs, const NativeFunctionDecl& rhs) {
    if (lhs.name == rhs.name && lhs.identity.usr.starts_with("c:") &&
        lhs.identity.usr == rhs.identity.usr) {
        return true;
    }
    return lhs.name == rhs.name && lhs.variadic == rhs.variadic &&
           lhs.deleted == rhs.deleted &&
           lhs.min_params == rhs.min_params && lhs.template_params == rhs.template_params &&
           lhs.template_param_is_value == rhs.template_param_is_value &&
           type_ref_lists_equivalent(lhs.template_default_args, rhs.template_default_args) &&
           type_ref_equivalent(native_function_return_type_ref(lhs),
                               native_function_return_type_ref(rhs)) &&
           type_ref_lists_equivalent(native_function_param_type_refs(lhs),
                                     native_function_param_type_refs(rhs));
}

bool contains_equivalent_native_function(const std::vector<NativeFunctionDecl>& functions,
                                         const NativeFunctionDecl& candidate) {
    return std::ranges::any_of(functions, [&](const NativeFunctionDecl& fn) {
        return native_function_equivalent(fn, candidate);
    });
}

void merge_native_class_declaration(ClassDecl& target, const ClassDecl& incoming) {
    ClassDecl source = incoming;
    if (!target.generic_params.empty() &&
        target.generic_params.size() == source.generic_params.size()) {
        const std::vector<std::string> canonical = canonical_template_params(target, source);
        rename_class_template_params(target, canonical);
        rename_class_template_params(source, canonical);
    }
    if (target.cpp_name.empty()) {
        target.cpp_name = source.cpp_name;
    }
    if (target.identity.usr.empty()) {
        target.identity.usr = source.identity.usr;
    }
    if (target.identity.canonical_path.empty()) {
        target.identity.canonical_path = source.identity.canonical_path;
    }
    if (!target.layout && source.layout) {
        target.layout = source.layout;
    }
    if (target.generic_params.empty()) {
        target.generic_params = source.generic_params;
    }
    if (source.generic_min_args &&
        (!target.generic_min_args || *source.generic_min_args < *target.generic_min_args)) {
        target.generic_min_args = source.generic_min_args;
    }
    if (target.generic_default_args.size() < source.generic_default_args.size()) {
        target.generic_default_args.resize(source.generic_default_args.size());
    }
    for (size_t i = 0; i < source.generic_default_args.size(); ++i) {
        if (!has_type_ref(target.generic_default_args[i]) &&
            has_type_ref(source.generic_default_args[i])) {
            target.generic_default_args[i] = source.generic_default_args[i];
        }
    }
    if (target.native_specialization_args.empty()) {
        target.native_specialization_args = source.native_specialization_args;
    }
    if (target.native_specialization_requirements.empty()) {
        target.native_specialization_requirements =
            source.native_specialization_requirements;
    }
    target.native_partial_specialization =
        target.native_partial_specialization || source.native_partial_specialization;
    target.native_declaration = target.native_declaration || source.native_declaration;
    if (target.origin_module.empty()) {
        target.origin_module = source.origin_module;
    }
    if (target.doc_comment.empty()) {
        target.doc_comment = source.doc_comment;
    }
    if (target.location.line == 0 && source.location.line != 0) {
        target.location = source.location;
    }

    for (const BaseClassDecl& base : source.base_class_refs) {
        if (!has_equivalent_base_class(target, base.type_ref)) {
            target.base_class_refs.push_back(base);
        }
    }
    std::set<std::string> fields;
    for (const FieldDecl& field : target.fields) {
        fields.insert(field.name);
    }
    for (const FieldDecl& field : source.fields) {
        if (fields.insert(field.name).second) {
            target.fields.push_back(field);
        }
    }
    std::map<std::string, size_t> static_fields;
    for (size_t i = 0; i < target.static_fields.size(); ++i) {
        static_fields.emplace(target.static_fields[i].name, i);
    }
    for (const ConstDecl& field : source.static_fields) {
        const auto existing = static_fields.find(field.name);
        if (existing == static_fields.end()) {
            static_fields.emplace(field.name, target.static_fields.size());
            target.static_fields.push_back(field);
        } else if (expr_missing(target.static_fields[existing->second].value_expr) &&
                   expr_present(field.value_expr)) {
            target.static_fields[existing->second].value_expr = field.value_expr;
        }
    }
    for (const FunctionDecl& method : source.methods) {
        if (!contains_equivalent_method(target.methods, method)) {
            target.methods.push_back(method);
        }
    }
    std::map<std::string, size_t> aliases;
    for (size_t i = 0; i < target.type_aliases.size(); ++i) {
        aliases.emplace(target.type_aliases[i].name, i);
    }
    for (const TypeAliasDecl& alias : source.type_aliases) {
        const auto existing = aliases.find(alias.name);
        if (existing == aliases.end()) {
            aliases.emplace(alias.name, target.type_aliases.size());
            target.type_aliases.push_back(alias);
        } else if (!has_type_ref(target.type_aliases[existing->second].type_ref) &&
                   has_type_ref(alias.type_ref)) {
            target.type_aliases[existing->second] = alias;
        }
    }
}

void append_unique_native_functions(std::vector<NativeFunctionDecl>& target,
                                    const std::vector<NativeFunctionDecl>& source) {
    for (const NativeFunctionDecl& item : source) {
        if (!contains_equivalent_native_function(target, item)) {
            target.push_back(item);
        }
    }
}

void append_unique_native_types(std::vector<NativeTypeDecl>& target,
                                const std::vector<NativeTypeDecl>& source) {
    std::map<std::string, std::string> aliases;
    for (const NativeTypeDecl& item : target) {
        if (!item.native_spelling.empty()) {
            aliases.insert_or_assign(item.name, item.native_spelling);
        }
    }
    for (const NativeTypeDecl& item : source) {
        if (!item.native_spelling.empty()) {
            aliases.insert_or_assign(item.name, item.native_spelling);
        }
    }
    for (const NativeTypeDecl& item : source) {
        const auto existing = std::ranges::find_if(
            target, [&](const NativeTypeDecl& candidate) { return candidate.name == item.name; });
        if (existing == target.end()) {
            target.push_back(item);
            continue;
        }
        const bool collision =
            native_decl_identity_key(*existing) != native_decl_identity_key(item) &&
            !native_type_redeclarations_compatible(*existing, item, aliases);
        if (collision && native_decl_collision_is_error(item.name, item.location)) {
            throw CompileError(
                item.location,
                "native type name collision: " + item.name + " (existing " +
                    native_decl_identity_key(*existing) + " as '" + existing->native_spelling +
                    "', incoming " + native_decl_identity_key(item) + " as '" +
                    item.native_spelling + "')");
        }
        if (!collision) {
            merge_native_type_declaration(*existing, item);
        }
    }
}

void append_unique_native_values(std::vector<NativeValueDecl>& target,
                                 const std::vector<NativeValueDecl>& source) {
    append_unique_native_decls(target, source, "value");
}

void append_unique_native_macros(std::vector<NativeMacroDecl>& target,
                                 const std::vector<NativeMacroDecl>& source) {
    append_unique_native_decls(target, source, "macro");
}

void append_unique_native_namespaces(std::vector<NativeNamespaceDecl>& target,
                                     const std::vector<NativeNamespaceDecl>& source) {
    append_unique_native_decls(target, source, "namespace");
}

void append_unique_native_classes(std::vector<ClassDecl>& target,
                                  const std::vector<ClassDecl>& source) {
    std::map<std::string, size_t> seen;
    for (size_t i = 0; i < target.size(); ++i) {
        seen[native_class_binding_key(target[i])] = i;
    }
    for (const ClassDecl& item : source) {
        const std::string binding = native_class_binding_key(item);
        const auto existing = seen.find(binding);
        if (existing == seen.end()) {
            seen.emplace(binding, target.size());
            target.push_back(item);
            continue;
        }
        ClassDecl& current = target[existing->second];
        if (native_decl_identity_key(current) != native_decl_identity_key(item) &&
            native_decl_collision_is_error(item.name, item.location)) {
            throw CompileError(item.location, "native class name collision: " + item.name);
        }
        merge_native_class_declaration(current, item);
    }
}

} // namespace dudu
