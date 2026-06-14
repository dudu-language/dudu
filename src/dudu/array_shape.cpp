#include "dudu/array_shape.hpp"

#include "dudu/cpp_lower.hpp"

#include <optional>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

size_t find_matching_bracket(std::string_view text, const size_t open) {
    int depth = 1;
    size_t cursor = open + 1;
    while (cursor < text.size() && depth > 0) {
        if (text[cursor] == '[') {
            ++depth;
        } else if (text[cursor] == ']') {
            --depth;
        }
        ++cursor;
    }
    return depth == 0 ? cursor - 1 : std::string::npos;
}

std::optional<std::string> inferred_array_element_type(const std::string& declared_type) {
    const std::string type = trim_copy(declared_type);
    if (!starts_with(type, "array[") || !ends_with(type, "]")) {
        return std::nullopt;
    }
    const size_t close = find_matching_bracket(type, 5);
    if (close != type.size() - 1) {
        return std::nullopt;
    }
    return trim_copy(type.substr(6, close - 6));
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
        return {
            .status = ArrayShapeStatus::EmptyLiteral, .type = {}, .element_type = *element_type};
    }
    const auto shape = literal_shape(value);
    if (!shape) {
        return {
            .status = ArrayShapeStatus::RaggedLiteral, .type = {}, .element_type = *element_type};
    }
    return {.status = ArrayShapeStatus::Inferred,
            .type = shaped_array_type(*element_type, *shape),
            .element_type = *element_type};
}

} // namespace dudu
