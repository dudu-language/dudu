#pragma once

#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct DuduFunctionOverload {
    const FunctionDecl* declaration = nullptr;
    FunctionSignature signature;
    std::vector<TypeRef> generic_args;
};

std::optional<DuduFunctionOverload>
select_dudu_function_overload(const FunctionScope& scope, const std::string& callee,
                              const std::vector<Expr>& args,
                              const std::vector<const FunctionDecl*>& declarations = {},
                              const std::optional<std::vector<TypeRef>>& explicit_type_args =
                                  std::nullopt);

} // namespace dudu
