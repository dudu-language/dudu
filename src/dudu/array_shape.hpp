#pragma once

#include "dudu/ast.hpp"

#include <string>
#include <vector>

namespace dudu {

enum class ArrayShapeStatus {
    NotInferredArray,
    Inferred,
    EmptyLiteral,
    RaggedLiteral,
};

struct ArrayShapeInference {
    ArrayShapeStatus status = ArrayShapeStatus::NotInferredArray;
    TypeRef type_ref;
    TypeRef element_type_ref;
    std::vector<size_t> shape;
};

ArrayShapeInference infer_array_literal_shape_type(const TypeRef& declared_type, const Expr& value);
TypeRef explicit_array_element_type_ref(const TypeRef& declared_type);
std::vector<size_t> explicit_array_shape(const TypeRef& declared_type);

} // namespace dudu
