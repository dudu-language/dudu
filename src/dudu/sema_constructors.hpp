#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_scope.hpp"

#include <functional>
#include <string>
#include <vector>

namespace dudu {

struct ConstructorParam {
    std::string name;
    TypeRef type_ref;
};

std::vector<ConstructorParam> constructor_params(const ClassDecl& klass);
void check_constructor_args_ast(
    const FunctionScope& scope, const ClassDecl& klass, const std::vector<Expr>& args,
    const SourceLocation* location,
    const std::function<TypeRef(const FunctionScope&, const Expr&, const SourceLocation*)>&
        infer_expr_type,
    const std::function<bool(const TypeRef&, const Expr&, const TypeRef&)>& can_assign);

} // namespace dudu
