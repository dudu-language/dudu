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
using NativeCanAssignFn =
    std::function<bool(const std::string&, const std::string&, const std::string&)>;

std::optional<FunctionSignature> native_signature_for_call(
    const FunctionScope& scope, const std::string& callee, const std::vector<std::string>& args,
    const SourceLocation* location, const NativeInferExprFn& infer_expr,
    const NativeCanAssignFn& can_assign);

} // namespace dudu
