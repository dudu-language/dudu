#include "dudu/sema_index_type_ref.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_ops.hpp"

#include <sstream>
#include <string_view>
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
    }
    type.value = value.str();
    type.location = element_type.location;
    type.range = element_type.range;
    type.text = substitute_type_ref_text(type, {});
    return type;
}

TypeRef named_type_ref(std::string name, const SourceLocation& location) {
    TypeRef type;
    type.kind = TypeKind::Named;
    type.name = std::move(name);
    type.location = location;
    type.text = type.name;
    return type;
}

TypeRef resolve_type_ref_alias(const Symbols& symbols, const TypeRef& raw_type) {
    if ((raw_type.kind == TypeKind::Named || raw_type.kind == TypeKind::Qualified ||
         raw_type.kind == TypeKind::Unknown) &&
        !type_ref_head_name(raw_type).empty()) {
        const std::string resolved = resolve_alias(symbols, substitute_type_ref_text(raw_type, {}));
        if (resolved != substitute_type_ref_text(raw_type, {})) {
            return parse_type_text(resolved, raw_type.location);
        }
    }
    return raw_type;
}

bool foreign_or_auto_indexable_type(const TypeRef& type) {
    const std::string rendered = substitute_type_ref_text(type, {});
    return rendered.empty() || rendered == "auto" || type.kind == TypeKind::Qualified ||
           rendered.find('.') != std::string::npos || rendered.find("::") != std::string::npos;
}

} // namespace

TypeRef array_element_template_type_ref(const SourceLocation& location, const TypeRef& array_type,
                                        std::string_view template_name) {
    TypeRef type;
    type.kind = TypeKind::Template;
    type.name = std::string(template_name);
    type.children.push_back(explicit_array_element_type_ref(array_type));
    type.location = location;
    type.text = substitute_type_ref_text(type, {});
    return type;
}

std::optional<TypeRef>
indexed_type_ref_from_type_ref_with_count(const Symbols& symbols, const SourceLocation& location,
                                          const TypeRef& raw_type,
                                          const size_t index_count, const bool is_slice,
                                          const bool has_step, const std::string& label) {
    const TypeRef resolved_type = resolve_type_ref_alias(symbols, raw_type);
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
    for (std::string_view name : {"list", "span", "strided_span", "set"}) {
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
            span.text = substitute_type_ref_text(span, {});
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
    if (type->kind == TypeKind::Template && type->name == "dict" && type->children.size() == 2) {
        return type->children[1];
    }
    if (type->kind == TypeKind::Template && type->children.size() == 1) {
        return type->children.front();
    }
    if (const auto signature = dudu_operator_signature(symbols, "[]",
                                                       substitute_type_ref_text(*type, {}))) {
        return signature_return_type_ref(*signature);
    }
    if (foreign_or_auto_indexable_type(*type)) {
        return named_type_ref("auto", location);
    }
    return std::nullopt;
}

} // namespace dudu
