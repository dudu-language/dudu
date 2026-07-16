#include "dudu/sema/sema_index_type_ref.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_ops.hpp"

#include <initializer_list>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

TypeRef shaped_array_type_ref(const TypeRef& element_type, const std::vector<size_t>& shape) {
    TypeRef storage;
    storage.kind = TypeKind::Template;
    storage.name = "array";
    storage.children.push_back(element_type);
    storage.location = element_type.location;
    storage.range = element_type.range;

    TypeRef type;
    type.kind = TypeKind::FixedArray;
    type.children.push_back(storage);
    std::ostringstream value;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            value << ", ";
        }
        value << shape[i];
        TypeRef dim;
        dim.kind = TypeKind::Value;
        dim.value = std::to_string(shape[i]);
        dim.location = element_type.location;
        dim.range = element_type.range;
        type.children.push_back(std::move(dim));
    }
    type.value = value.str();
    type.location = element_type.location;
    type.range = element_type.range;
    return type;
}

TypeRef shaped_array_type_ref(const TypeRef& element_type, const std::vector<std::string>& shape) {
    TypeRef storage;
    storage.kind = TypeKind::Template;
    storage.name = "array";
    storage.children.push_back(element_type);
    storage.location = element_type.location;
    storage.range = element_type.range;

    TypeRef type;
    type.kind = TypeKind::FixedArray;
    type.children.push_back(storage);
    std::ostringstream value;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            value << ", ";
        }
        const std::string dim_value = trim_string(shape[i]);
        value << dim_value;
        TypeRef dim;
        dim.kind = TypeKind::Value;
        dim.value = dim_value;
        dim.location = element_type.location;
        dim.range = element_type.range;
        type.children.push_back(std::move(dim));
    }
    type.value = value.str();
    type.location = element_type.location;
    type.range = element_type.range;
    return type;
}

bool foreign_or_auto_indexable_type(const TypeRef& type) {
    const std::string head = type_ref_head_name(type);
    return !has_type_ref(type) || type_ref_is_auto(type) || type.kind == TypeKind::Qualified ||
           head.find('.') != std::string::npos || head.find("::") != std::string::npos;
}

bool integer_extent_ref(const TypeRef& type) {
    if (type.kind != TypeKind::Value || type.value.empty()) {
        return false;
    }
    for (const char ch : type.value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

TypeRef unwrap_index_receiver_type(TypeRef type) {
    while ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const ||
            type.kind == TypeKind::Pointer || type.kind == TypeKind::Storage ||
            type.kind == TypeKind::Shared || type.kind == TypeKind::Device ||
            type.kind == TypeKind::Volatile || type.kind == TypeKind::Atomic) &&
           type.children.size() == 1) {
        TypeRef child = type.children.front();
        type = std::move(child);
    }
    return type;
}

bool native_fixed_extent_template(const Symbols& symbols, const TypeRef& type,
                                  const bool native_alias) {
    if (type.kind != TypeKind::Template || type.children.size() < 2) {
        return false;
    }
    const std::string head = type_ref_head_name(type);
    if (!native_alias && !symbols.native_types.contains(head) &&
        !is_native_class_binding(symbols, head) && head.find('.') == std::string::npos &&
        head.find("::") == std::string::npos) {
        return false;
    }
    for (size_t index = 1; index < type.children.size(); ++index) {
        if (!integer_extent_ref(type.children[index])) {
            return false;
        }
    }
    return true;
}

bool template_head_is(const TypeRef& type, std::initializer_list<std::string_view> names) {
    if (type.kind != TypeKind::Template) {
        return false;
    }
    for (std::string_view name : names) {
        if (type.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

TypeRef array_element_template_type_ref(const SourceLocation& location, const TypeRef& array_type,
                                        std::string_view template_name) {
    TypeRef type;
    type.kind = TypeKind::Template;
    type.name = std::string(template_name);
    type.children.push_back(explicit_array_element_type_ref(array_type));
    type.location = location;
    return type;
}

std::optional<TypeRef> indexed_type_ref_from_type_ref_with_count(
    const Symbols& symbols, const SourceLocation& location, const TypeRef& receiver_type,
    const size_t index_count, const bool is_slice, const bool has_step, const std::string& label) {
    const TypeRef unwrapped_receiver_type = unwrap_index_receiver_type(receiver_type);
    const bool receiver_is_native_alias =
        symbols.native_types.contains(type_ref_head_name(unwrapped_receiver_type));
    const TypeRef resolved_type = resolve_alias_ref(symbols, receiver_type);
    const TypeRef* type = &resolved_type;
    while ((type->kind == TypeKind::Reference || type->kind == TypeKind::Const) &&
           type->children.size() == 1) {
        type = &type->children.front();
    }
    bool pointer_index = false;
    if (type->kind == TypeKind::Pointer && type->children.size() == 1) {
        type = &type->children.front();
        pointer_index = true;
    }
    while ((type->kind == TypeKind::Reference || type->kind == TypeKind::Const ||
            type->kind == TypeKind::Storage || type->kind == TypeKind::Shared ||
            type->kind == TypeKind::Device || type->kind == TypeKind::Volatile ||
            type->kind == TypeKind::Atomic) &&
           type->children.size() == 1) {
        type = &type->children.front();
    }
    if (pointer_index) {
        return *type;
    }
    for (std::string_view name : {"list", "span", "strided_span", "array_view", "set"}) {
        const std::vector<TypeRef> args = template_type_arg_refs(*type, name);
        if (args.size() == 1) {
            return args.front();
        }
    }
    if (const std::vector<size_t> shape = explicit_array_shape(*type); !shape.empty()) {
        const TypeRef element = explicit_array_element_type_ref(*type);
        if (is_slice) {
            if (shape.size() != 1) {
                throw CompileError(location,
                                   "array slicing requires one-dimensional fixed array: " + label);
            }
            TypeRef span;
            span.kind = TypeKind::Template;
            span.name = has_step ? "strided_span" : "span";
            span.children.push_back(element);
            span.location = location;
            return span;
        }
        if (index_count > shape.size()) {
            throw CompileError(location, "too many indices for array: " + label);
        }
        if (index_count == shape.size()) {
            return element;
        }
        return shaped_array_type_ref(element,
                                     std::vector<size_t>(shape.begin() + index_count, shape.end()));
    }
    if (const std::vector<std::string> shape = explicit_array_shape_values(*type); !shape.empty()) {
        const TypeRef element = explicit_array_element_type_ref(*type);
        if (is_slice) {
            if (shape.size() != 1) {
                throw CompileError(location,
                                   "array slicing requires one-dimensional fixed array: " + label);
            }
            TypeRef span;
            span.kind = TypeKind::Template;
            span.name = has_step ? "strided_span" : "span";
            span.children.push_back(element);
            span.location = location;
            return span;
        }
        if (index_count > shape.size()) {
            throw CompileError(location, "too many indices for array: " + label);
        }
        if (index_count == shape.size()) {
            return element;
        }
        return shaped_array_type_ref(
            element, std::vector<std::string>(shape.begin() + index_count, shape.end()));
    }
    if (template_head_is(
            *type, {"dict", "std.map", "std::map", "std.unordered_map", "std::unordered_map"}) &&
        type->children.size() == 2) {
        if (is_slice || index_count != 1) {
            throw CompileError(location, "map indexing requires exactly one key: " + label);
        }
        return type->children[1];
    }
    if (native_fixed_extent_template(symbols, *type, receiver_is_native_alias)) {
        return type->children.front();
    }
    if (type->kind == TypeKind::Template && type->children.size() == 1) {
        return type->children.front();
    }
    if (foreign_or_auto_indexable_type(*type)) {
        return named_type_ref("auto", location);
    }
    return std::nullopt;
}

} // namespace dudu
