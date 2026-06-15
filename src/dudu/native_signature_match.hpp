#pragma once

#include "dudu/sema_native.hpp"

namespace dudu {

std::optional<std::pair<std::string, std::vector<std::string>>>
native_template_call_base(const std::string& callee);

std::optional<FunctionSignature> match_native_signature(const FunctionScope& scope,
                                                        const std::string& callee,
                                                        const std::vector<Expr>& args,
                                                        const SourceLocation* location,
                                                        const NativeInferExprAstFn& infer_expr,
                                                        const NativeCanAssignAstFn& can_assign);

} // namespace dudu
