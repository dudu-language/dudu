#include "dudu/native_signature_templates.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <cctype>
#include <set>

namespace dudu {
namespace {

std::string type_ref_binding_text(const TypeRef& type) {
    return substitute_type_ref_text(type, {});
}

bool binding_equivalent(const TypeRef& left, const TypeRef& right) {
    return type_ref_equivalent(left, right) ||
           type_ref_binding_text(left) == type_ref_binding_text(right);
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
    return type;
}

std::string join_type_ref_binding_texts(const std::vector<TypeRef>& types) {
    std::string out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += type_ref_binding_text(types[i]);
    }
    return out;
}

std::string strip_pack_marker_suffix(std::string text) {
    text = trim(std::move(text));
    while (!text.empty() && text.back() == '.') {
        text = trim(text.substr(0, text.size() - 1));
    }
    return text;
}

bool native_pack_marker(const TypeRef& type) {
    return trim(type_ref_binding_text(type)) == ".";
}

bool bind_template_pack_expansion(const std::vector<TypeRef>& expected,
                                  const std::vector<TypeRef>& got,
                                  NativeTemplateBindings& bindings) {
    if (expected.size() < 2) {
        return false;
    }
    const std::string pack_name = strip_pack_marker_suffix(type_ref_binding_text(expected.front()));
    if (!native_template_placeholder(pack_name)) {
        return false;
    }
    for (size_t i = 1; i < expected.size(); ++i) {
        if (!native_pack_marker(expected[i])) {
            return false;
        }
    }
    const std::string expected_expansion = join_type_ref_binding_texts(expected);
    const std::string got_expansion = join_type_ref_binding_texts(got);
    TypeRef got_expansion_ref;
    got_expansion_ref.text = got_expansion;
    got_expansion_ref.location = got.front().location;
    return bind_template_placeholder(expected_expansion, std::move(got_expansion_ref), bindings);
}

bool bind_template_type_ref(const TypeRef& expected, const TypeRef& got,
                            NativeTemplateBindings& bindings);

bool bind_same_shape_children(const TypeRef& expected, const TypeRef& got,
                              NativeTemplateBindings& bindings) {
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
    const std::string expected_name =
        expected.name.empty() ? trim_copy(expected.text) : trim_copy(expected.name);
    if ((expected.kind == TypeKind::Named || expected.kind == TypeKind::Qualified ||
         expected.kind == TypeKind::Value) &&
        native_template_placeholder(expected_name.empty() ? trim_copy(expected.value)
                                                          : expected_name)) {
        return bind_template_placeholder(
            expected_name.empty() ? trim_copy(expected.value) : expected_name, got, bindings);
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
        if (bind_template_pack_expansion(expected.children, got.children, bindings)) {
            return true;
        }
        return bind_same_shape_children(expected, got, bindings);
    }
    if (expected.kind == TypeKind::FixedArray && got.kind == TypeKind::FixedArray &&
        expected.value == got.value) {
        return bind_same_shape_children(expected, got, bindings);
    }
    return false;
}

} // namespace

bool native_template_placeholder(const std::string& type) {
    static const std::set<std::string> simple = {
        "T", "U", "V", "A", "B", "L", "Q", "Key", "Value", "__i", "__j", "_Int", "_Index", "_Nm"};
    if (simple.contains(type)) {
        return true;
    }
    if (type.size() == 1 && std::isupper(static_cast<unsigned char>(type.front())) != 0) {
        return true;
    }
    return type.size() > 1 && type.front() == '_' &&
           std::isupper(static_cast<unsigned char>(type[1])) != 0;
}

std::optional<std::string> native_template_pack_placeholder(std::string type) {
    type = strip_forwarding_suffix(std::move(type));
    if (native_template_placeholder(type)) {
        return type;
    }
    return std::nullopt;
}

bool bind_native_template_type_ast(const TypeRef& expected, const TypeRef& got,
                                   NativeTemplateBindings& bindings) {
    return bind_template_type_ref(expected, got, bindings);
}

bool bind_native_template_type_ast(const Symbols& symbols, const TypeRef& expected,
                                   const TypeRef& got, NativeTemplateBindings& bindings) {
    TypeRef resolved_got = resolve_alias_ref(symbols, got);
    return bind_template_type_ref(expected, resolved_got, bindings);
}

} // namespace dudu
