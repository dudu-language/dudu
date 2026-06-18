#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_scope.hpp"

#include <functional>
#include <string>
#include <vector>

namespace dudu {

struct BodyCheckCallbacks {
    std::function<TypeRef(const FunctionScope&, const Expr&, const SourceLocation*)>
        infer_expr_type;
    std::function<bool(const FunctionScope&, const TypeRef&, const Expr&, const TypeRef&)>
        can_assign_type;
};

void check_bodies(const ModuleAst& module, const Symbols& symbols,
                  const BodyCheckCallbacks& callbacks);

} // namespace dudu
