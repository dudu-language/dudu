#include "dudu/sema/type_compat_structural.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/native/native_header_identity.hpp"

namespace dudu {
namespace {

bool type_refs_equivalent_ignoring_c_tags(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != got.kind || expected.reference_kind != got.reference_kind ||
        expected.children.size() != got.children.size()) {
        return false;
    }
    switch (expected.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Template:
    case TypeKind::Associated:
    case TypeKind::AssociatedTemplate:
    case TypeKind::NativeTransform:
        if (native_type_name_without_tag(type_ref_head_name(expected)) !=
            native_type_name_without_tag(type_ref_head_name(got))) {
            return false;
        }
        break;
    case TypeKind::Value:
        if (trim_string(expected.value) != trim_string(got.value)) {
            return false;
        }
        break;
    case TypeKind::FixedArray:
    case TypeKind::Shaped:
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
    if (expected.kind != TypeKind::FixedArray || got.kind != TypeKind::FixedArray) {
        return false;
    }
    const std::vector<TypeRef> expected_shape = explicit_array_shape_refs(expected);
    const std::vector<TypeRef> got_shape = explicit_array_shape_refs(got);
    if (expected_shape.empty() || got_shape.empty() ||
        !type_ref_equivalent(expected_shape.front(), got_shape.front())) {
        return false;
    }
    return structural_type_assignment_allowed(fixed_array_child_type_ref(expected),
                                              fixed_array_child_type_ref(got));
}

bool shaped_dim_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind == TypeKind::Value && trim_string(expected.value) == "dyn") {
        return true;
    }
    return type_ref_equivalent(expected, got);
}

bool structural_shaped_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind == TypeKind::Shaped && got.kind == TypeKind::Shaped) {
        if (expected.children.empty() || got.children.empty()) {
            return false;
        }
        if (expected.children.size() != got.children.size()) {
            return false;
        }
        for (size_t i = 1; i < expected.children.size(); ++i) {
            if (!shaped_dim_assignment_allowed(expected.children[i], got.children[i])) {
                return false;
            }
        }
        return structural_type_assignment_allowed(expected.children.front(), got.children.front());
    }
    if (expected.kind == TypeKind::Shaped && !expected.children.empty()) {
        return structural_type_assignment_allowed(expected.children.front(), got);
    }
    if (got.kind == TypeKind::Shaped && !got.children.empty()) {
        return structural_type_assignment_allowed(expected, got.children.front());
    }
    return false;
}

bool structural_pointer_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    if (expected.kind != TypeKind::Pointer || got.kind != TypeKind::Pointer ||
        expected.children.size() != 1 || got.children.size() != 1) {
        return false;
    }
    const TypeRef& expected_pointee = expected.children.front();
    const TypeRef& got_pointee = got.children.front();
    if (type_ref_is_void(expected_pointee) ||
        (expected_pointee.kind == TypeKind::Const && expected_pointee.children.size() == 1 &&
         type_ref_is_void(expected_pointee.children.front()))) {
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
    if (type_refs_equivalent_ignoring_c_tags(expected, got)) {
        return true;
    }
    if (type_ref_is_auto(expected) || type_ref_is_auto(got)) {
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
    if (expected.kind == TypeKind::Shaped || got.kind == TypeKind::Shaped) {
        return structural_shaped_assignment_allowed(expected, got);
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
