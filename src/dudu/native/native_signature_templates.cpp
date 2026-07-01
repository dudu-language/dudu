#include "dudu/native/native_signature_templates.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/sema/type_compat_native.hpp"
#include "dudu/sema/type_compat_structural.hpp"

#include <cctype>
#include <set>

namespace dudu {
namespace {

bool binding_equivalent(const TypeRef& left, const TypeRef& right) {
    const TypeRef normalized_left = normalize_cpp_type_artifacts_ref(left);
    const TypeRef normalized_right = normalize_cpp_type_artifacts_ref(right);
    return type_ref_equivalent(normalized_left, normalized_right) ||
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
                            NativeTemplateBindings& bindings);

std::optional<std::string> native_template_pack_placeholder_spelling(std::string type) {
    type = strip_forwarding_suffix(std::move(type));
    if (type.ends_with("...")) {
        type.erase(type.size() - 3);
        type = trim_copy(type);
    }
    if (native_template_placeholder(type)) {
        return type;
    }
    return std::nullopt;
}

bool bind_template_pack_child(const TypeRef& expected, const std::vector<TypeRef>& got,
                              NativeTemplateBindings& bindings) {
    if (expected.kind != TypeKind::PackExpansion || expected.children.size() != 1) {
        return false;
    }
    const std::optional<std::string> pack_name = native_template_pack_placeholder(expected);
    if (!pack_name) {
        return false;
    }
    if (got.empty()) {
        return true;
    }
    return bind_template_placeholder(*pack_name, got.front(), bindings);
}

bool bind_same_shape_children(const TypeRef& expected, const TypeRef& got,
                              NativeTemplateBindings& bindings) {
    if (expected.children.size() == 1 &&
        bind_template_pack_child(expected.children.front(), got.children, bindings)) {
        return true;
    }
    if (expected.children.size() != got.children.size()) {
        return false;
    }
    for (size_t i = 0; i < expected.children.size(); ++i) {
        if (!bind_template_type_ref(expected.children[i], got.children[i], bindings)) {
            return false;
        }
    }
    return true;
}

bool same_native_template_name(const std::string& expected, const std::string& got) {
    return expected == got || expected.ends_with("." + got) || got.ends_with("." + expected);
}

bool bind_template_type_ref(const TypeRef& expected, const TypeRef& got,
                            NativeTemplateBindings& bindings) {
    if (got.kind == TypeKind::Shaped && expected.kind != TypeKind::Shaped &&
        !got.children.empty()) {
        return bind_template_type_ref(expected, got.children.front(), bindings);
    }

    const std::string expected_name = type_ref_head_name(expected);
    if ((expected.kind == TypeKind::Named || expected.kind == TypeKind::Qualified ||
         expected.kind == TypeKind::Value) &&
        native_template_placeholder(expected_name)) {
        return bind_template_placeholder(expected_name, got, bindings);
    }
    if (expected.kind == TypeKind::Pointer) {
        return got.kind == TypeKind::Pointer && expected.children.size() == 1 &&
               got.children.size() == 1 &&
               bind_template_type_ref(expected.children.front(), got.children.front(), bindings);
    }
    if (expected.kind == TypeKind::Reference) {
        if (expected.children.size() != 1) {
            return false;
        }
        return got.kind == TypeKind::Reference && got.children.size() == 1
                   ? bind_template_type_ref(expected.children.front(), got.children.front(),
                                            bindings)
                   : bind_template_type_ref(expected.children.front(), got, bindings);
    }
    if (expected.kind == TypeKind::Const || expected.kind == TypeKind::Volatile ||
        expected.kind == TypeKind::Atomic || expected.kind == TypeKind::Storage ||
        expected.kind == TypeKind::Shared || expected.kind == TypeKind::Device) {
        if (expected.children.size() != 1) {
            return false;
        }
        return got.kind == expected.kind && got.children.size() == 1
                   ? bind_template_type_ref(expected.children.front(), got.children.front(),
                                            bindings)
                   : bind_template_type_ref(expected.children.front(), got, bindings);
    }
    if (expected.kind == TypeKind::Template && got.kind == TypeKind::Template &&
        same_native_template_name(expected.name, got.name)) {
        return bind_same_shape_children(expected, got, bindings);
    }
    if ((expected.kind == TypeKind::FixedArray || expected.kind == TypeKind::Shaped) &&
        expected.kind == got.kind) {
        return bind_same_shape_children(expected, got, bindings);
    }
    return false;
}

} // namespace

bool native_template_placeholder(const std::string& type) {
    const size_t qualifier = type.find_last_of(".:");
    if (qualifier != std::string::npos && qualifier + 1 < type.size()) {
        return native_template_placeholder(type.substr(qualifier + 1));
    }
    static const std::set<std::string> simple = {
        "T", "U", "V", "A", "B", "L", "Q", "Key", "Value", "__i", "__j", "_Int", "_Index", "_Nm"};
    if (simple.contains(type)) {
        return true;
    }
    if (!type.empty() && std::isupper(static_cast<unsigned char>(type.front())) != 0) {
        return true;
    }
    return type.size() > 1 && type.front() == '_' &&
           std::isupper(static_cast<unsigned char>(type[1])) != 0;
}

std::optional<std::string> native_template_pack_placeholder(const TypeRef& type) {
    if (type.kind == TypeKind::PackExpansion && type.children.size() == 1) {
        return native_template_pack_placeholder(type.children.front());
    }
    if (type.kind == TypeKind::Named) {
        return native_template_pack_placeholder_spelling(type.name);
    }
    if (type.kind == TypeKind::Template &&
        (type.name == "__decay_and_strip" || type.name == "std.remove_reference" ||
         type.name == "std::remove_reference") &&
        type.children.size() == 1) {
        return native_template_pack_placeholder(type.children.front());
    }
    if ((type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference ||
         type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
         type.kind == TypeKind::Atomic || type.kind == TypeKind::Storage ||
         type.kind == TypeKind::Shared || type.kind == TypeKind::Device) &&
        type.children.size() == 1) {
        return native_template_pack_placeholder(type.children.front());
    }
    if (const std::optional<std::string> placeholder =
            native_template_pack_placeholder_spelling(type_ref_head_name(type))) {
        return placeholder;
    }
    return std::nullopt;
}

bool bind_native_template_type_ast(const TypeRef& expected, const TypeRef& got,
                                   NativeTemplateBindings& bindings) {
    return bind_template_type_ref(expected, got, bindings);
}

bool bind_native_template_type_ast(const Symbols& symbols, const TypeRef& expected,
                                   const TypeRef& got, NativeTemplateBindings& bindings) {
    TypeRef resolved_expected = resolve_alias_ref(symbols, expected);
    TypeRef resolved_got = resolve_alias_ref(symbols, got);
    return bind_template_type_ref(resolved_expected, resolved_got, bindings);
}

} // namespace dudu
