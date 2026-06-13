#pragma once

#include "dudu/sema_scope.hpp"
#include "dudu/source.hpp"

#include <functional>
#include <optional>
#include <string>

namespace dudu {

using InferExprFn =
    std::function<std::string(const FunctionScope&, std::string, const SourceLocation*)>;

bool is_string_literal_expr(const std::string& expr);
std::optional<std::string> infer_not_expr(const FunctionScope& scope, const std::string& expr,
                                          const SourceLocation* location,
                                          const InferExprFn& infer_expr);
std::optional<std::string> infer_logical_expr(const FunctionScope& scope, const std::string& expr,
                                              const SourceLocation* location,
                                              const InferExprFn& infer_expr);
std::optional<std::string> infer_comparison_expr(const FunctionScope& scope,
                                                 const std::string& expr,
                                                 const SourceLocation* location,
                                                 const InferExprFn& infer_expr);

} // namespace dudu
