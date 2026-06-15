#include "dudu/type_compat_structural.hpp"

#include "dudu/cpp_lower.hpp"

#include <cctype>

namespace dudu {
namespace {

std::string compact_type(std::string type) {
    std::string out;
    for (const char c : type) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            out.push_back(c);
        }
    }
    return out;
}

std::string normalize_c_tags(std::string type) {
    for (std::string_view tag : {"struct ", "class ", "union ", "enum "}) {
        size_t pos = type.find(tag);
        while (pos != std::string::npos) {
            type.erase(pos, tag.size());
            pos = type.find(tag, pos);
        }
    }
    return type;
}

bool same_type_name(const TypeRef& expected, const TypeRef& got) {
    return compact_type(normalize_c_tags(trim_copy(expected.text))) ==
           compact_type(normalize_c_tags(trim_copy(got.text)));
}

bool same_type_name(std::string expected, const TypeRef& got) {
    return compact_type(normalize_c_tags(std::move(expected))) ==
           compact_type(normalize_c_tags(trim_copy(got.text)));
}

bool is_transparent_wrapper(const TypeKind kind) {
    return kind == TypeKind::Atomic || kind == TypeKind::Volatile || kind == TypeKind::Device ||
           kind == TypeKind::Storage || kind == TypeKind::Shared;
}

bool structural_template_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != TypeKind::Template || got.kind != TypeKind::Template ||
        expected.name != got.name || expected.children.size() != got.children.size()) {
        return false;
    }
    for (size_t i = 0; i < expected.children.size(); ++i) {
        if (!structural_type_assignment_allowed(expected.children[i], got.children[i])) {
            return false;
        }
    }
    return true;
}

bool structural_function_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != TypeKind::Function || got.kind != TypeKind::Function ||
        expected.children.size() != got.children.size()) {
        return false;
    }
    for (size_t i = 0; i < expected.children.size(); ++i) {
        if (!structural_type_assignment_allowed(expected.children[i], got.children[i])) {
            return false;
        }
    }
    return true;
}

bool structural_pointer_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != TypeKind::Pointer || got.kind != TypeKind::Pointer ||
        expected.children.size() != 1 || got.children.size() != 1) {
        return false;
    }
    const TypeRef& expected_pointee = expected.children.front();
    const TypeRef& got_pointee = got.children.front();
    if (same_type_name("void", expected_pointee) ||
        (expected_pointee.kind == TypeKind::Const && expected_pointee.children.size() == 1 &&
         same_type_name("void", expected_pointee.children.front()))) {
        return true;
    }
    if (expected_pointee.kind == TypeKind::Const && expected_pointee.children.size() == 1 &&
        structural_type_assignment_allowed(expected_pointee.children.front(), got_pointee)) {
        return true;
    }
    if (got_pointee.kind == TypeKind::Reference && got_pointee.children.size() == 1 &&
        structural_type_assignment_allowed(expected_pointee, got_pointee.children.front())) {
        return true;
    }
    return structural_type_assignment_allowed(expected_pointee, got_pointee);
}

bool structural_reference_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind == TypeKind::Reference && expected.children.size() == 1) {
        const TypeRef& target = expected.children.front();
        if (target.kind == TypeKind::Const && target.children.size() == 1) {
            return structural_type_assignment_allowed(target.children.front(), got);
        }
        return structural_type_assignment_allowed(target, got);
    }
    if (got.kind == TypeKind::Reference && got.children.size() == 1) {
        const TypeRef& target = got.children.front();
        if (target.kind == TypeKind::Const && target.children.size() == 1) {
            return structural_type_assignment_allowed(expected, target.children.front());
        }
        return structural_type_assignment_allowed(expected, target);
    }
    return false;
}

} // namespace

bool structural_type_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind == TypeKind::Unknown || got.kind == TypeKind::Unknown) {
        return false;
    }
    if (same_type_name(expected, got)) {
        return true;
    }
    if (same_type_name("auto", expected) || same_type_name("auto", got)) {
        return true;
    }
    if ((expected.kind == TypeKind::Pointer || got.kind == TypeKind::Pointer) &&
        structural_pointer_assignment_allowed(expected, got)) {
        return true;
    }
    if ((expected.kind == TypeKind::Reference || got.kind == TypeKind::Reference) &&
        structural_reference_assignment_allowed(expected, got)) {
        return true;
    }
    if (expected.kind == TypeKind::Function || got.kind == TypeKind::Function) {
        return structural_function_assignment_allowed(expected, got);
    }
    if (expected.kind == TypeKind::Template || got.kind == TypeKind::Template) {
        return structural_template_assignment_allowed(expected, got);
    }
    if (expected.kind == got.kind && expected.children.size() == 1 && got.children.size() == 1 &&
        (expected.kind == TypeKind::Const || is_transparent_wrapper(expected.kind))) {
        return structural_type_assignment_allowed(expected.children.front(), got.children.front());
    }
    if (is_transparent_wrapper(expected.kind) && expected.children.size() == 1) {
        return structural_type_assignment_allowed(expected.children.front(), got);
    }
    if (is_transparent_wrapper(got.kind) && got.children.size() == 1) {
        return structural_type_assignment_allowed(expected, got.children.front());
    }
    return false;
}

} // namespace dudu
