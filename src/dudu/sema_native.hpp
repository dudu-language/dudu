#pragma once

#include "dudu/sema_scope.hpp"
#include "dudu/source.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

using NativeInferExprFn =
    std::function<std::string(const FunctionScope&, std::string, const SourceLocation*)>;
using NativeInferExprAstFn =
    std::function<std::string(const FunctionScope&, const Expr&, const SourceLocation*)>;
using NativeCanAssignFn =
    std::function<bool(const std::string&, const std::string&, const std::string&)>;
using NativeCanAssignAstFn =
    std::function<bool(const std::string&, const Expr&, const std::string&)>;

std::optional<FunctionSignature>
native_signature_for_call(const FunctionScope& scope, const std::string& callee,
                          const std::vector<std::string>& args, const SourceLocation* location,
                          const NativeInferExprFn& infer_expr, const NativeCanAssignFn& can_assign);
std::optional<FunctionSignature> native_signature_for_call(const FunctionScope& scope,
                                                           const std::string& callee,
                                                           const std::vector<Expr>& args,
                                                           const SourceLocation* location,
                                                           const NativeInferExprAstFn& infer_expr,
                                                           const NativeCanAssignAstFn& can_assign);

} // namespace dudu
