#pragma once

#include "dudu/sema_scope.hpp"
#include "dudu/source.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

using NativeInferExprAstFn =
    std::function<std::string(const FunctionScope&, const Expr&, const SourceLocation*)>;
using NativeCanAssignAstFn =
    std::function<bool(const std::string&, const Expr&, const std::string&)>;

std::optional<FunctionSignature> native_signature_for_call(const FunctionScope& scope,
                                                           const std::string& callee,
                                                           const std::vector<Expr>& args,
                                                           const SourceLocation* location,
                                                           const NativeInferExprAstFn& infer_expr,
                                                           const NativeCanAssignAstFn& can_assign);
bool foreign_cpp_type_name(const Symbols& symbols, const std::string& type);
bool native_import_path_prefix(const Symbols& symbols, const std::string& path);
std::optional<std::string> native_member_path_type(const Symbols& symbols,
                                                   const std::string& path);
std::optional<std::string> native_member_expr_type(const Symbols& symbols, const Expr& expr);

} // namespace dudu
