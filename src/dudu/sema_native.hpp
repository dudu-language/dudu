#pragma once

#include "dudu/sema_scope.hpp"
#include "dudu/source.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

using NativeInferExprTypeAstFn =
    std::function<TypeRef(const FunctionScope&, const Expr&, const SourceLocation*)>;
using NativeCanAssignAstFn = std::function<bool(const TypeRef&, const Expr&, const TypeRef&)>;

std::optional<FunctionSignature>
native_signature_for_call(const FunctionScope& scope, const std::string& callee,
                          const std::vector<TypeRef>& explicit_template_args,
                          const std::vector<Expr>& args, const SourceLocation* location,
                          const NativeInferExprTypeAstFn& infer_expr_type,
                          const NativeCanAssignAstFn& can_assign);
bool foreign_cpp_type_name(const Symbols& symbols, const TypeRef& type);
bool native_import_path_prefix(const Symbols& symbols, const std::string& path);
std::optional<TypeRef> native_member_path_type_ref(const Symbols& symbols, const std::string& path,
                                                   SourceLocation location = {});
std::optional<TypeRef> native_member_expr_type_ref(const Symbols& symbols, const Expr& expr,
                                                   SourceLocation location = {});

} // namespace dudu
