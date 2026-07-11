#pragma once

#include "dudu/sema/sema_scope.hpp"

namespace dudu {

struct NativeSignatureMatch {
    FunctionSignature signature;
    const NativeFunctionDecl* declaration = nullptr;
};

std::optional<NativeSignatureMatch>
match_native_signature_declaration(const FunctionScope& scope, const std::string& callee,
                                   const std::vector<TypeRef>& explicit_template_args,
                                   const std::vector<Expr>& args,
                                   const SourceLocation* location);

std::optional<FunctionSignature>
match_native_signature(const FunctionScope& scope, const std::string& callee,
                       const std::vector<TypeRef>& explicit_template_args,
                       const std::vector<Expr>& args, const SourceLocation* location);

} // namespace dudu
