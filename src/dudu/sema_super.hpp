#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_scope.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct SuperCheckCallbacks {
    std::function<TypeRef(const FunctionScope&, const Expr&, const SourceLocation*)>
        infer_expr_type;
    std::function<bool(const FunctionScope&, const TypeRef&, const Expr&, const TypeRef&)>
        can_assign;
    std::function<std::optional<FunctionSignature>(
        const FunctionScope&, const std::vector<FunctionSignature>&, const std::vector<Expr>&)>
        matching_signature;
};

bool is_super_call(const std::string& callee);
bool is_super_init_stmt(const Stmt& stmt);
TypeRef infer_super_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                  const std::string& callee, const SourceLocation* location,
                                  const SuperCheckCallbacks& callbacks);

} // namespace dudu
