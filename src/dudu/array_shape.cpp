#include "dudu/array_shape.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <optional>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

std::optional<std::string> inferred_array_element_type(const std::string& declared_type) {
    const TypeRef type = parse_type_text(declared_type);
    if (type.kind != TypeKind::Template || type.name != "array" || type.children.size() != 1) {
        return std::nullopt;
    }
    return trim_copy(type.children.front().text);
}

std::optional<std::pair<std::string, std::vector<size_t>>>
explicit_array_type_info(const std::string& declared_type) {
    const TypeRef type = parse_type_text(declared_type);
    if (type.kind != TypeKind::FixedArray || type.children.empty()) {
        return std::nullopt;
    }
    const TypeRef& storage = type.children.front();
    if (storage.kind != TypeKind::Template || storage.name != "array" ||
        storage.children.size() != 1) {
        return std::nullopt;
    }
    std::vector<size_t> shape;
    const std::vector<std::string> dims = split_top_level_args(type.value);
    for (const std::string& dim : dims) {
        if (dim.empty()) {
            return std::nullopt;
        }
        size_t value = 0;
        for (const char c : dim) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            value = value * 10 + static_cast<size_t>(c - '0');
        }
        shape.push_back(value);
    }
    return std::pair{trim_copy(storage.children.front().text), shape};
}

std::optional<std::vector<size_t>> literal_shape(const Expr& expr) {
    if (expr.kind != ExprKind::ListLiteral) {
        return std::vector<size_t>{};
    }
    if (expr.children.empty()) {
        return std::nullopt;
    }
    std::optional<std::vector<size_t>> child_shape;
    for (const Expr& child : expr.children) {
        const auto shape = literal_shape(child);
        if (!shape) {
            return std::nullopt;
        }
        if (!child_shape) {
            child_shape = *shape;
        } else if (*child_shape != *shape) {
            return std::nullopt;
        }
    }
    std::vector<size_t> out = {expr.children.size()};
    if (child_shape) {
        out.insert(out.end(), child_shape->begin(), child_shape->end());
    }
    return out;
}

std::string shaped_array_type(const std::string& element_type, const std::vector<size_t>& shape) {
    std::ostringstream out;
    out << "array[" << element_type << "][";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << shape[i];
    }
    out << "]";
    return out.str();
}

} // namespace

ArrayShapeInference infer_array_literal_shape_type(const std::string& declared_type,
                                                   const Expr& value) {
    const auto element_type = inferred_array_element_type(declared_type);
    if (!element_type) {
        return {};
    }
    if (value.kind != ExprKind::ListLiteral) {
        return {};
    }
    if (value.children.empty()) {
        return {.status = ArrayShapeStatus::EmptyLiteral,
                .type = {},
                .element_type = *element_type,
                .shape = {}};
    }
    const auto shape = literal_shape(value);
    if (!shape) {
        return {.status = ArrayShapeStatus::RaggedLiteral,
                .type = {},
                .element_type = *element_type,
                .shape = {}};
    }
    return {.status = ArrayShapeStatus::Inferred,
            .type = shaped_array_type(*element_type, *shape),
            .element_type = *element_type,
            .shape = *shape};
}

std::vector<size_t> explicit_array_shape(const std::string& declared_type) {
    const auto info = explicit_array_type_info(declared_type);
    return info ? info->second : std::vector<size_t>{};
}

std::string explicit_array_element_type(const std::string& declared_type) {
    const auto info = explicit_array_type_info(declared_type);
    return info ? info->first : std::string{};
}

} // namespace dudu
