#include "dudu/core/array_shape.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/shape_value_expr.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace dudu {
namespace {

std::optional<TypeRef> inferred_array_element_type_ref(const TypeRef& type) {
    if (type.kind != TypeKind::Template || type.name != "array" || type.children.size() != 1) {
        return std::nullopt;
    }
    return type.children.front();
}

std::optional<std::vector<size_t>> explicit_array_shape_from_type(const TypeRef& type) {
    if (type.kind != TypeKind::FixedArray || type.children.size() < 2) {
        return std::nullopt;
    }
    std::vector<size_t> shape;
    for (size_t i = 1; i < type.children.size(); ++i) {
        const TypeRef& dim = type.children[i];
        if (dim.kind != TypeKind::Value || dim.value.empty()) {
            return std::nullopt;
        }
        const auto value = shape_value_expr_eval(dim.value);
        if (!value || *value < 0) {
            return std::nullopt;
        }
        shape.push_back(static_cast<size_t>(*value));
    }
    return shape;
}

std::optional<std::vector<TypeRef>> explicit_array_shape_refs_from_type(const TypeRef& type) {
    if (type.kind != TypeKind::FixedArray || type.children.size() < 2) {
        return std::nullopt;
    }
    const TypeRef& storage = type.children.front();
    if (storage.kind != TypeKind::Template || storage.name != "array" ||
        storage.children.size() != 1) {
        return std::nullopt;
    }
    std::vector<TypeRef> shape;
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (!has_type_ref(type.children[i])) {
            return std::nullopt;
        }
        shape.push_back(type.children[i]);
    }
    return shape;
}

struct LiteralShape {
    bool valid = true;
    std::vector<size_t> shape;
    SourceLocation error_location;
};

LiteralShape literal_shape(const Expr& expr) {
    if (expr.kind != ExprKind::ListLiteral) {
        return {};
    }
    if (expr.children.empty()) {
        return {.valid = true, .shape = {0}, .error_location = {}};
    }
    std::optional<std::vector<size_t>> child_shape;
    for (const Expr& child : expr.children) {
        const LiteralShape child_result = literal_shape(child);
        if (!child_result.valid) {
            return child_result;
        }
        if (!child_shape) {
            child_shape = child_result.shape;
        } else if (*child_shape != child_result.shape) {
            return {.valid = false, .shape = {}, .error_location = child.location};
        }
    }
    std::vector<size_t> out = {expr.children.size()};
    if (child_shape) {
        out.insert(out.end(), child_shape->begin(), child_shape->end());
    }
    return {.valid = true, .shape = std::move(out), .error_location = {}};
}

std::string shape_value_text(const std::vector<size_t>& shape) {
    std::ostringstream out;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << shape[i];
    }
    return out.str();
}

TypeRef shape_value_ref(size_t value, const SourceLocation& location) {
    TypeRef type;
    type.kind = TypeKind::Value;
    type.value = std::to_string(value);
    type.location = location;
    type.range.start = location;
    type.range.end = location;
    return type;
}

TypeRef shaped_array_type_ref(const TypeRef& declared_type, const std::vector<size_t>& shape) {
    TypeRef type;
    type.kind = TypeKind::FixedArray;
    type.value = shape_value_text(shape);
    type.children.push_back(declared_type);
    for (const size_t dim : shape) {
        type.children.push_back(shape_value_ref(dim, declared_type.location));
    }
    type.location = declared_type.location;
    type.range = declared_type.range;
    return type;
}

} // namespace

ArrayShapeInference infer_array_literal_shape_type(const TypeRef& declared_type,
                                                   const Expr& value) {
    const auto element_type = inferred_array_element_type_ref(declared_type);
    if (!element_type) {
        return {};
    }
    if (value.kind != ExprKind::ListLiteral) {
        return {};
    }
    if (value.children.empty()) {
        return {.status = ArrayShapeStatus::EmptyLiteral,
                .type_ref = {},
                .element_type_ref = *element_type,
                .shape = {},
                .error_location = value.location};
    }
    const LiteralShape literal = literal_shape(value);
    if (!literal.valid) {
        return {.status = ArrayShapeStatus::RaggedLiteral,
                .type_ref = {},
                .element_type_ref = *element_type,
                .shape = {},
                .error_location = literal.error_location};
    }
    return {.status = ArrayShapeStatus::Inferred,
            .type_ref = shaped_array_type_ref(declared_type, literal.shape),
            .element_type_ref = *element_type,
            .shape = literal.shape,
            .error_location = {}};
}

SourceLocation array_shape_mismatch_location(const Expr& value,
                                             const std::vector<size_t>& expected,
                                             const std::vector<size_t>& actual) {
    size_t axis = 0;
    const size_t common_rank = std::min(expected.size(), actual.size());
    while (axis < common_rank && expected[axis] == actual[axis]) {
        ++axis;
    }
    const Expr* item = &value;
    for (size_t depth = 0; depth < axis && item->kind == ExprKind::ListLiteral &&
                           !item->children.empty();
         ++depth) {
        item = &item->children.front();
    }
    return item->location;
}

std::vector<TypeRef> explicit_array_shape_refs(const TypeRef& declared_type) {
    const auto shape = explicit_array_shape_refs_from_type(declared_type);
    return shape.value_or(std::vector<TypeRef>{});
}

std::vector<size_t> explicit_array_shape(const TypeRef& declared_type) {
    const auto shape = explicit_array_shape_from_type(declared_type);
    return shape.value_or(std::vector<size_t>{});
}

std::vector<std::string> explicit_array_shape_values(const TypeRef& declared_type) {
    std::vector<std::string> out;
    for (const TypeRef& dim : explicit_array_shape_refs(declared_type)) {
        out.push_back(substitute_type_ref_text(dim, {}));
    }
    return out;
}

TypeRef explicit_array_element_type_ref(const TypeRef& declared_type) {
    if (declared_type.kind != TypeKind::FixedArray || declared_type.children.empty()) {
        return {};
    }
    const TypeRef& storage = declared_type.children.front();
    if (storage.kind != TypeKind::Template || storage.name != "array" ||
        storage.children.size() != 1) {
        return {};
    }
    return storage.children.front();
}

} // namespace dudu
