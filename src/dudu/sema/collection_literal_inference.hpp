#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"

#include <functional>
#include <string>

namespace dudu {

enum class CollectionLiteralStatus {
    NotCollection,
    Inferred,
    Empty,
    Heterogeneous,
    Unresolved,
    Malformed,
};

struct CollectionLiteralInference {
    CollectionLiteralStatus status = CollectionLiteralStatus::NotCollection;
    TypeRef type_ref;
    TypeRef expected_ref;
    TypeRef actual_ref;
    SourceLocation error_location;
    std::string component;
};

using CollectionElementTypeInfer = std::function<TypeRef(const Expr&)>;

bool is_collection_literal(const Expr& expr);
CollectionLiteralInference
infer_collection_literal_type(const Symbols* symbols, const Expr& expr,
                              const CollectionElementTypeInfer& infer_element);
std::string collection_literal_error(const CollectionLiteralInference& inference);

} // namespace dudu
