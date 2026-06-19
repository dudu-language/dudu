#pragma once

#include "dudu/sema_native.hpp"

namespace dudu {

std::optional<FunctionSignature>
match_native_signature(const FunctionScope& scope, const std::string& callee,
                       const std::vector<TypeRef>& explicit_template_args,
                       const std::vector<Expr>& args, const SourceLocation* location,
                       const NativeInferExprTypeAstFn& infer_expr_type,
                       const NativeCanAssignAstFn& can_assign);

} // namespace dudu
