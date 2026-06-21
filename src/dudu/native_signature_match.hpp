#pragma once

#include "dudu/sema_scope.hpp"

namespace dudu {

std::optional<FunctionSignature>
match_native_signature(const FunctionScope& scope, const std::string& callee,
                       const std::vector<TypeRef>& explicit_template_args,
                       const std::vector<Expr>& args, const SourceLocation* location);

} // namespace dudu
