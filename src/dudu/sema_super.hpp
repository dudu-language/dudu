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
    std::function<bool(const FunctionScope&, const std::string&, const Expr&, const std::string&)>
        can_assign;
    std::function<std::optional<FunctionSignature>(
        const FunctionScope&, const std::vector<FunctionSignature>&, const std::vector<Expr>&)>
        matching_signature;
    std::function<void(const FunctionScope&, const std::string&, const FunctionSignature&,
                       const std::vector<Expr>&, const SourceLocation*)>
        check_call_args;
};

bool is_super_call(const std::string& callee);
bool is_super_init_stmt(const Stmt& stmt);
TypeRef infer_super_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                  const std::string& callee, const SourceLocation* location,
                                  const SuperCheckCallbacks& callbacks);

} // namespace dudu
