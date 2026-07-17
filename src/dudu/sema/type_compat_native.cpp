#include "dudu/sema/type_compat_native.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/sema/type_compat_structural.hpp"

#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

bool type_ref_is_native_char(const TypeRef& type) {
    return type_ref_is_name(type, "char") || type_ref_is_name(type, "i8");
}

bool is_native_numeric_type_name(const std::string& type) {
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

bool is_native_numeric_type(const TypeRef& type) {
    return is_native_numeric_type_name(type_ref_head_name(normalize_cpp_type_artifacts_ref(type)));
}

std::string native_type_head_spelling(const TypeRef& type) {
    return trim_string(type_ref_head_name(type));
}

std::string native_type_tail_name(const TypeRef& type) {
    std::string name = native_type_head_spelling(type);
    const size_t dot = name.find_last_of(".:");
    if (dot != std::string::npos) {
        name = name.substr(dot + 1);
    }
    return name;
}

bool native_type_head_ends_with_name(const TypeRef& type, const std::string& name) {
    const std::string spelling = native_type_head_spelling(type);
    return spelling == name || spelling.ends_with("." + name) || spelling.ends_with("::" + name);
}

TypeRef normalize_tuple_element(const TypeRef& type) {
    if (type.kind == TypeKind::Reference && type.children.size() == 1) {
        TypeRef out = type;
        out.children[0] = normalize_tuple_element(type.children.front());
        return out;
    }
    if (type.kind != TypeKind::Template || type.name != "__tuple_element_t" ||
        type.children.size() != 2 || type.children[0].kind != TypeKind::Value) {
        TypeRef out = type;
        for (TypeRef& child : out.children) {
            child = normalize_tuple_element(child);
        }
        return out;
    }
    const TypeRef& tuple_type = type.children[1];
    if (tuple_type.kind != TypeKind::Template ||
        (tuple_type.name != "std.tuple" && tuple_type.name != "tuple")) {
        return type;
    }
    const std::string index_text = trim_string(type.children[0].value);
    if (index_text.empty() || index_text.find_first_not_of("0123456789") != std::string::npos) {
        return type;
    }
    const size_t index = static_cast<size_t>(std::stoull(index_text));
    if (index >= tuple_type.children.size()) {
        return type;
    }
    return normalize_tuple_element(tuple_type.children[index]);
}

std::optional<TypeKind> builtin_wrapper_template_kind(const std::string& name) {
    if (name == "atomic" || name == "std.atomic" || name == "std::atomic") {
        return TypeKind::Atomic;
    }
    if (name == "volatile") {
        return TypeKind::Volatile;
    }
    if (name == "storage") {
        return TypeKind::Storage;
    }
    if (name == "shared") {
        return TypeKind::Shared;
    }
    if (name == "device") {
        return TypeKind::Device;
    }
    return std::nullopt;
}

std::string normalize_cpp_primitive_type(std::string type) {
    type = trim_string(std::move(type));
    const size_t qualifier = type.find_last_of(".:");
    if (qualifier != std::string::npos && qualifier + 1 < type.size()) {
        const std::string tail = type.substr(qualifier + 1);
        const std::string normalized_tail = normalize_cpp_primitive_type(tail);
        if (normalized_tail != tail) {
            return normalized_tail;
        }
    }
    if (type == "bool") {
        return "bool";
    }
    if (type == "char" || type == "signed char" || type == "int8_t" || type == "std.int8_t") {
        return "i8";
    }
    if (type == "unsigned char" || type == "uint8_t" || type == "std.uint8_t") {
        return "u8";
    }
    if (type == "short" || type == "short int" || type == "int16_t" || type == "std.int16_t") {
        return "i16";
    }
    if (type == "unsigned short" || type == "unsigned short int" || type == "uint16_t" ||
        type == "std.uint16_t") {
        return "u16";
    }
    if (type == "int" || type == "signed int" || type == "int32_t" || type == "std.int32_t") {
        return "i32";
    }
    if (type == "unsigned int" || type == "uint32_t" || type == "std.uint32_t") {
        return "u32";
    }
    if (type == "long long" || type == "long long int" || type == "int64_t" ||
        type == "std.int64_t") {
        return "i64";
    }
    if (type == "unsigned long long" || type == "unsigned long long int" || type == "uint64_t" ||
        type == "std.uint64_t") {
        return "u64";
    }
    if (type == "float") {
        return "f32";
    }
    if (type == "double") {
        return "f64";
    }
    if (type == "size_t" || type == "std.size_t" || type == "std::size_t") {
        return "usize";
    }
    if (type == "ptrdiff_t" || type == "std.ptrdiff_t" || type == "std::ptrdiff_t") {
        return "isize";
    }
    return type;
}

TypeRef normalize_cpp_primitive_type_ref(const TypeRef& type, bool collapse_string_alias) {
    TypeRef out = type;
    for (TypeRef& child : out.children) {
        child = normalize_cpp_primitive_type_ref(child, collapse_string_alias);
    }

    if (out.kind == TypeKind::Named || out.kind == TypeKind::Qualified) {
        const std::string original = type_ref_head_name(out);
        const std::string normalized = normalize_cpp_primitive_type(original);
        if (normalized != original) {
            return named_type_ref(normalized, out.location);
        }
        return out;
    }

    if (out.kind == TypeKind::Template) {
        if (const std::optional<TypeKind> wrapper = builtin_wrapper_template_kind(out.name);
            wrapper.has_value() && out.children.size() == 1) {
            return wrapped_type_ref(*wrapper, out.children.front(), out.location);
        }
        if (collapse_string_alias &&
            (out.name == "basic_string" || out.name == "std.basic_string" ||
             out.name == "std::basic_string") &&
            !out.children.empty() && type_ref_is_native_char(out.children.front())) {
            return named_type_ref("std.string", out.location);
        }
        const std::string original = type_ref_head_name(out);
        const std::string normalized = normalize_cpp_primitive_type(original);
        if (normalized != original) {
            out.name = normalized;
            out.kind = normalized.find('.') == std::string::npos &&
                               normalized.find("::") == std::string::npos
                           ? TypeKind::Named
                           : TypeKind::Qualified;
        }
        return out;
    }

    return out;
}

TypeRef collapse_cpp_references(TypeRef type) {
    for (TypeRef& child : type.children) {
        child = collapse_cpp_references(std::move(child));
    }
    while (type.kind == TypeKind::Reference && type.children.size() == 1 &&
           type.children.front().kind == TypeKind::Reference &&
           type.children.front().children.size() == 1) {
        TypeRef inner = std::move(type.children.front());
        if (type.reference_kind == ReferenceKind::Lvalue ||
            inner.reference_kind == ReferenceKind::Lvalue) {
            type.reference_kind = ReferenceKind::Lvalue;
        } else {
            type.reference_kind = ReferenceKind::Rvalue;
        }
        type.children = std::move(inner.children);
    }
    return type;
}

} // namespace

TypeRef normalize_cpp_type_structure_ref(const TypeRef& type) {
    return collapse_cpp_references(
        normalize_cpp_primitive_type_ref(normalize_tuple_element(type), false));
}

TypeRef normalize_cpp_type_artifacts_ref(const TypeRef& type) {
    return collapse_cpp_references(
        normalize_cpp_primitive_type_ref(normalize_tuple_element(type), true));
}

bool native_associated_type_assignment_allowed(const TypeRef& expected, const TypeRef& got) {
    const TypeRef normalized_expected = normalize_cpp_type_artifacts_ref(expected);
    const TypeRef normalized_got = normalize_cpp_type_artifacts_ref(got);
    if (type_ref_equivalent(normalized_expected, normalized_got) ||
        structural_type_assignment_allowed(normalized_expected, normalized_got)) {
        return true;
    }

    static const std::set<std::string> associated = {
        "iterator", "const_iterator", "reference", "const_reference", "value_type",
        "pointer",  "const_pointer",  "size_type", "difference_type"};
    const std::string expected_name = native_type_tail_name(normalized_expected);
    const std::string got_name = native_type_tail_name(normalized_got);
    if (!associated.contains(expected_name) && !associated.contains(got_name)) {
        return false;
    }
    if ((expected_name == "size_type" || expected_name == "difference_type") &&
        is_native_numeric_type(normalized_got)) {
        return true;
    }
    if ((got_name == "size_type" || got_name == "difference_type") &&
        is_native_numeric_type(normalized_expected)) {
        return true;
    }
    if (native_type_head_ends_with_name(normalized_got, expected_name)) {
        return true;
    }
    return expected_name == "const_iterator" &&
           native_type_head_ends_with_name(normalized_got, "iterator");
}

bool native_numeric_operator_operand(const TypeRef& type) {
    const TypeRef normalized = normalize_cpp_type_artifacts_ref(type);
    if (is_native_numeric_type(normalized)) {
        return true;
    }
    const std::string name = native_type_tail_name(normalized);
    return name == "size_type" || name == "difference_type";
}

bool native_associated_operator_operand_is_dependent(const TypeRef& type) {
    static const std::set<std::string> operator_dependent = {"reference", "const_reference",
                                                             "iterator", "const_iterator"};
    return operator_dependent.contains(
        native_type_tail_name(normalize_cpp_type_artifacts_ref(type)));
}

} // namespace dudu
