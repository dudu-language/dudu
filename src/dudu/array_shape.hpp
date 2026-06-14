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
    std::string element_type;
    std::vector<size_t> shape;
};

ArrayShapeInference infer_array_literal_shape_type(const std::string& declared_type,
                                                   const Expr& value);
std::string explicit_array_element_type(const std::string& declared_type);
std::vector<size_t> explicit_array_shape(const std::string& declared_type);

} // namespace dudu
