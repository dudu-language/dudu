#pragma once

#include "dudu/ast.hpp"

#include <string>

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
};

ArrayShapeInference infer_array_literal_shape_type(const std::string& declared_type,
                                                   const Expr& value);

} // namespace dudu
