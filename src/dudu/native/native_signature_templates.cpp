#include "dudu/native/native_signature_templates.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/sema/type_compat_native.hpp"
#include "dudu/sema/type_compat_structural.hpp"

#include <stdexcept>

namespace dudu {
namespace {

bool binding_equivalent(const TypeRef& left, const TypeRef& right) {
    const TypeRef normalized_left = normalize_cpp_type_artifacts_ref(left);
    const TypeRef normalized_right = normalize_cpp_type_artifacts_ref(right);
    return type_ref_equivalent(normalized_left, normalized_right) ||
           type_ref_text(normalized_left) == type_ref_text(normalized_right) ||
           structural_type_assignment_allowed(normalized_left, normalized_right);
}

bool bind_template_placeholder(const std::string& name, TypeRef got,
                               NativeTemplateBindings& bindings) {
    const auto found = bindings.find(name);
    if (found == bindings.end()) {
        bindings[name] = std::move(got);
        return true;
    }
    return binding_equivalent(found->second, got);
}

std::string strip_forwarding_suffix(std::string type) {
    type = trim(std::move(type));
    if (type.ends_with("...")) {
        type = trim(type.substr(0, type.size() - 3));
    }
    while (!type.empty() && type.back() == '&') {
        type = trim(type.substr(0, type.size() - 1));
    }
    while (!type.empty() && (type.front() == '&' || type.front() == '*')) {
        type = trim(type.substr(1));
    }
    return type;
}

bool bind_template_type_ref(const TypeRef& expected, const TypeRef& got,
                            const NativeTemplateParameterNames& template_params,
                            NativeTemplateBindings& bindings,
                            NativePackBindingMap& pack_bindings);

std::string normalized_template_param_name(std::string name) {
    const size_t qualifier = name.find_last_of(".:");
    if (qualifier != std::string::npos && qualifier + 1 < name.size()) {
        name = name.substr(qualifier + 1);
    }
    if (name.ends_with("...")) {
        name.resize(name.size() - 3);
    }
    return name;
}

bool declared_template_param(std::string name,
                             const NativeTemplateParameterNames& template_params) {
    return template_params.contains(normalized_template_param_name(std::move(name)));
}

std::optional<std::string>
native_template_pack_placeholder_spelling(std::string type,
                                          const NativeTemplateParameterNames& template_params) {
    type = strip_forwarding_suffix(std::move(type));
    if (type.ends_with("...")) {
        type.erase(type.size() - 3);
        type = trim_copy(type);
    }
    type = normalized_template_param_name(std::move(type));
    if (template_params.contains(type)) {
        return type;
    }
    return std::nullopt;
}

bool bind_template_pack_child(const TypeRef& expected, const std::vector<TypeRef>& got,
                              const NativeTemplateParameterNames& template_params,
                              NativePackBindingMap& pack_bindings) {
    if (expected.kind != TypeKind::PackExpansion || expected.children.size() != 1) {
        return false;
    }
    const std::optional<std::string> pack_name =
        native_template_pack_placeholder(expected, template_params);
    if (!pack_name) {
        return false;
    }
    const auto [found, inserted] = pack_bindings.emplace(*pack_name, got);
    if (inserted || found->second.size() != got.size()) {
        return inserted;
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (!binding_equivalent(found->second[i], got[i])) {
            return false;
        }
    }
    return true;
}

bool bind_same_shape_children(const TypeRef& expected, const TypeRef& got,
                              const NativeTemplateParameterNames& template_params,
                              NativeTemplateBindings& bindings,
                              NativePackBindingMap& pack_bindings) {
    if (expected.children.size() == 1 &&
        bind_template_pack_child(expected.children.front(), got.children, template_params,
                                 pack_bindings)) {
        return true;
    }
    if (expected.children.size() != got.children.size()) {
        return false;
    }
    for (size_t i = 0; i < expected.children.size(); ++i) {
        if (!bind_template_type_ref(expected.children[i], got.children[i], template_params,
                                    bindings, pack_bindings)) {
            return false;
        }
    }
    return true;
}

bool same_native_template_name(const std::string& expected, const std::string& got) {
    return expected == got || expected.ends_with("." + got) || got.ends_with("." + expected);
}

bool bind_template_type_ref(const TypeRef& expected, const TypeRef& got,
                            const NativeTemplateParameterNames& template_params,
                            NativeTemplateBindings& bindings,
                            NativePackBindingMap& pack_bindings) {
    if (got.kind == TypeKind::Shaped && expected.kind != TypeKind::Shaped &&
        !got.children.empty()) {
        return bind_template_type_ref(expected, got.children.front(), template_params, bindings,
                                      pack_bindings);
    }

    const std::string expected_name = type_ref_head_name(expected);
    if ((expected.kind == TypeKind::Named || expected.kind == TypeKind::Qualified ||
         expected.kind == TypeKind::Value) &&
        declared_template_param(expected_name, template_params)) {
        return bind_template_placeholder(normalized_template_param_name(expected_name), got,
                                         bindings);
    }
    if (expected.kind == TypeKind::Pointer) {
        return got.kind == TypeKind::Pointer && expected.children.size() == 1 &&
               got.children.size() == 1 &&
               bind_template_type_ref(expected.children.front(), got.children.front(),
                                      template_params, bindings, pack_bindings);
    }
    if (expected.kind == TypeKind::Reference) {
        if (expected.children.size() != 1) {
            return false;
        }
        return got.kind == TypeKind::Reference && got.children.size() == 1
                   ? bind_template_type_ref(expected.children.front(), got.children.front(),
                                            template_params, bindings, pack_bindings)
                   : bind_template_type_ref(expected.children.front(), got, template_params,
                                            bindings, pack_bindings);
    }
    if (expected.kind == TypeKind::Const || expected.kind == TypeKind::Volatile ||
        expected.kind == TypeKind::Atomic || expected.kind == TypeKind::Storage ||
        expected.kind == TypeKind::Shared || expected.kind == TypeKind::Device) {
        if (expected.children.size() != 1) {
            return false;
        }
        return got.kind == expected.kind && got.children.size() == 1
                   ? bind_template_type_ref(expected.children.front(), got.children.front(),
                                            template_params, bindings, pack_bindings)
                   : bind_template_type_ref(expected.children.front(), got, template_params,
                                            bindings, pack_bindings);
    }
    if (expected.kind == TypeKind::Template && got.kind == TypeKind::Template &&
        same_native_template_name(expected.name, got.name)) {
        return bind_same_shape_children(expected, got, template_params, bindings, pack_bindings);
    }
    if ((expected.kind == TypeKind::FixedArray || expected.kind == TypeKind::Shaped) &&
        expected.kind == got.kind) {
        return bind_same_shape_children(expected, got, template_params, bindings, pack_bindings);
    }
    return false;
}

} // namespace

NativeTemplateParameterNames
native_type_template_parameters(const FunctionSignature& signature) {
    if (signature.template_params.size() != signature.template_param_is_value.size()) {
        throw std::logic_error("native function template parameter metadata is incomplete");
    }
    NativeTemplateParameterNames out;
    for (size_t i = 0; i < signature.template_params.size(); ++i) {
        if (!signature.template_param_is_value[i]) {
            out.insert(normalized_template_param_name(signature.template_params[i]));
        }
    }
    return out;
}

std::optional<std::string>
native_template_pack_placeholder(const TypeRef& type,
                                 const NativeTemplateParameterNames& template_params) {
    if (type.kind == TypeKind::PackExpansion && type.children.size() == 1) {
        return native_template_pack_placeholder(type.children.front(), template_params);
    }
    if (type.kind == TypeKind::Named) {
        return native_template_pack_placeholder_spelling(type.name, template_params);
    }
    if ((type.kind == TypeKind::Template || type.kind == TypeKind::Associated ||
         type.kind == TypeKind::AssociatedTemplate) &&
        type.children.size() == 1) {
        return native_template_pack_placeholder(type.children.front(), template_params);
    }
    if ((type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference ||
         type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
         type.kind == TypeKind::Atomic || type.kind == TypeKind::Storage ||
         type.kind == TypeKind::Shared || type.kind == TypeKind::Device) &&
        type.children.size() == 1) {
        return native_template_pack_placeholder(type.children.front(), template_params);
    }
    if (const std::optional<std::string> placeholder =
            native_template_pack_placeholder_spelling(type_ref_head_name(type), template_params)) {
        return placeholder;
    }
    return std::nullopt;
}

bool bind_native_template_type_ast(const TypeRef& expected, const TypeRef& got,
                                   const NativeTemplateParameterNames& template_params,
                                   NativeTemplateBindings& bindings) {
    NativePackBindingMap pack_bindings;
    return bind_template_type_ref(expected, got, template_params, bindings, pack_bindings);
}

bool bind_native_template_type_ast(const Symbols& symbols, const TypeRef& expected,
                                   const TypeRef& got,
                                   const NativeTemplateParameterNames& template_params,
                                   NativeTemplateBindings& bindings) {
    NativePackBindingMap pack_bindings;
    return bind_native_template_type_ast(symbols, expected, got, template_params, bindings,
                                         pack_bindings);
}

bool bind_native_template_type_ast(const Symbols& symbols, const TypeRef& expected,
                                   const TypeRef& got,
                                   const NativeTemplateParameterNames& template_params,
                                   NativeTemplateBindings& bindings,
                                   NativePackBindingMap& pack_bindings) {
    NativeTemplateBindings direct_bindings = bindings;
    NativePackBindingMap direct_pack_bindings = pack_bindings;
    if (bind_template_type_ref(expected, got, template_params, direct_bindings,
                               direct_pack_bindings)) {
        bindings = std::move(direct_bindings);
        pack_bindings = std::move(direct_pack_bindings);
        return true;
    }
    TypeRef resolved_expected = resolve_alias_ref(symbols, expected);
    TypeRef resolved_got = resolve_alias_ref(symbols, got);
    return bind_template_type_ref(resolved_expected, resolved_got, template_params, bindings,
                                  pack_bindings);
}

} // namespace dudu
