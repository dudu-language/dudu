#pragma once

#include "dudu/array_shape.hpp"
#include "dudu/ast.hpp"
#include "dudu/sema_scope.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

struct EffectiveVarType {
    TypeRef ref;
    bool inferred = false;
};

bool freestanding_like(const FunctionScope& scope);
bool is_array_literal(const Expr& expr);
bool function_has_decorator(const FunctionDecl& fn, std::string_view name);

void check_type_ref_match(FunctionScope& scope, const TypeRef& expected, const Expr& expr,
                          const SourceLocation& location, std::string_view mismatch_label = {});
void check_array_literal_elements(FunctionScope& scope, const TypeRef& element_type,
                                  const Expr& expr, const SourceLocation& location);

EffectiveVarType effective_var_type(const Stmt& stmt, const ArrayShapeInference& inferred);
std::string shape_display(const std::vector<size_t>& shape);
TypeRef const_reference_type_ref(TypeRef type);
void check_condition_type(FunctionScope& scope, const Stmt& stmt);
std::optional<TypeRef> infer_for_binding_type(FunctionScope& scope, const Stmt& stmt);

} // namespace dudu
