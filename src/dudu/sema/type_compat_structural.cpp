#include "dudu/sema/type_compat_structural.hpp"

#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_lower.hpp"

namespace dudu {
namespace {

std::string strip_c_tag_prefix(std::string type) {
    type = trim_copy(std::move(type));
    for (std::string_view tag : {"struct ", "class ", "union ", "enum "}) {
        if (type.starts_with(tag)) {
            return trim_copy(type.substr(tag.size()));
        }
    }
    return type;
}

bool type_refs_equivalent_ignoring_c_tags(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != got.kind || expected.children.size() != got.children.size()) {
        return false;
    }
    switch (expected.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Template:
        if (strip_c_tag_prefix(type_ref_head_name(expected)) !=
            strip_c_tag_prefix(type_ref_head_name(got))) {
            return false;
        }
        break;
    case TypeKind::Value:
        if (trim_copy(expected.value) != trim_copy(got.value)) {
            return false;
        }
        break;
    case TypeKind::FixedArray:
        break;
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Const:
    case TypeKind::Volatile:
    case TypeKind::Atomic:
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
    case TypeKind::Static:
    case TypeKind::Function:
    case TypeKind::PackExpansion:
        break;
    case TypeKind::Unknown:
        return false;
    }
    for (size_t i = 0; i < expected.children.size(); ++i) {
        if (!type_refs_equivalent_ignoring_c_tags(expected.children[i], got.children[i])) {
            return false;
        }
    }
    return true;
}

bool same_type_name(const TypeRef& expected, const TypeRef& got) {
    return type_refs_equivalent_ignoring_c_tags(expected, got);
}

bool same_type_name(std::string expected, const TypeRef& got) {
    return type_refs_equivalent_ignoring_c_tags(named_type_ref(std::move(expected)), got);
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

bool structural_fixed_array_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != TypeKind::FixedArray || got.kind != TypeKind::FixedArray ||
        expected.children.size() < 2 || got.children.size() < 2 ||
        expected.children.size() != got.children.size()) {
        return false;
    }
    for (size_t i = 1; i < expected.children.size(); ++i) {
        if (!type_ref_equivalent(expected.children[i], got.children[i])) {
            return false;
        }
    }
    return structural_type_assignment_allowed(expected.children.front(), got.children.front());
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
        if (got.kind == TypeKind::Reference && got.children.size() == 1 &&
            got.children.front().kind == TypeKind::Const) {
            return false;
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
    if (expected.kind == TypeKind::FixedArray || got.kind == TypeKind::FixedArray) {
        return structural_fixed_array_assignment_allowed(expected, got);
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
