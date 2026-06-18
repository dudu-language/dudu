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
    std::string type;
    TypeRef type_ref;
    std::string element_type;
    TypeRef element_type_ref;
    std::vector<size_t> shape;
};

ArrayShapeInference infer_array_literal_shape_type(const std::string& declared_type,
                                                   const Expr& value);
ArrayShapeInference infer_array_literal_shape_type(const TypeRef& declared_type, const Expr& value);
std::string explicit_array_element_type(const std::string& declared_type);
std::string explicit_array_element_type(const TypeRef& declared_type);
TypeRef explicit_array_element_type_ref(const TypeRef& declared_type);
std::vector<size_t> explicit_array_shape(const std::string& declared_type);
std::vector<size_t> explicit_array_shape(const TypeRef& declared_type);

} // namespace dudu
