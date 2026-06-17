#include "dudu/native_signature_templates.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <cctype>
#include <set>

namespace dudu {
namespace {

bool bind_template_placeholder(const std::string& name, const std::string& got,
                               NativeTemplateBindings& bindings) {
    const auto found = bindings.find(name);
    if (found == bindings.end()) {
        bindings[name] = got;
        return true;
    }
    return found->second == got;
}

std::optional<std::pair<std::string, std::string>> wrapped_type(std::string type) {
    type = trim(std::move(type));
    const size_t open = type.find('[');
    if (open == std::string::npos || !type.ends_with("]")) {
        return std::nullopt;
    }
    return std::make_pair(trim(type.substr(0, open)),
                          trim(type.substr(open + 1, type.size() - open - 2)));
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

std::string type_ref_binding_text(const TypeRef& type) {
    return substitute_type_ref_text(type, {});
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
        return bind_template_placeholder(expected_name.empty() ? trim_copy(expected.value)
                                                               : expected_name,
                                         type_ref_binding_text(got), bindings);
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

bool bind_native_template_type(std::string expected, std::string got,
                               NativeTemplateBindings& bindings) {
    expected = trim(std::move(expected));
    got = trim(std::move(got));
    if (native_template_placeholder(expected)) {
        return bind_template_placeholder(expected, got, bindings);
    }
    if (!expected.empty() && expected.front() == '&') {
        if (!got.empty() && got.front() == '&') {
            got = trim(got.substr(1));
        }
        return bind_native_template_type(expected.substr(1), std::move(got), bindings);
    }
    if (!expected.empty() && expected.front() == '*') {
        if (got.empty() || got.front() != '*') {
            return false;
        }
        return bind_native_template_type(expected.substr(1), got.substr(1), bindings);
    }
    const std::optional<std::pair<std::string, std::string>> expected_wrap = wrapped_type(expected);
    if (expected_wrap.has_value()) {
        if (const std::optional<std::pair<std::string, std::string>> got_wrap = wrapped_type(got);
            got_wrap.has_value() && got_wrap->first == expected_wrap->first) {
            got = got_wrap->second;
        }
        return bind_native_template_type(expected_wrap->second, std::move(got), bindings);
    }
    return false;
}

bool bind_native_template_type_ast(const TypeRef& expected, const TypeRef& got,
                                   NativeTemplateBindings& bindings) {
    return bind_template_type_ref(expected, got, bindings);
}

bool bind_native_template_type_ast(const Symbols& symbols, const std::string& expected,
                                   const std::string& got, NativeTemplateBindings& bindings) {
    const TypeRef expected_ref = parse_type_text(expected);
    const TypeRef got_ref = parse_type_text(resolve_alias(symbols, got));
    return bind_template_type_ref(expected_ref, got_ref, bindings);
}

} // namespace dudu
