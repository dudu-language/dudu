#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <string>
#include <vector>

namespace dudu {

struct ConstructorParam {
    std::string name;
    TypeRef type_ref;
};

bool class_uses_aggregate_initialization(const ClassDecl& klass);
std::vector<ConstructorParam> constructor_params(const ClassDecl& klass);
const FunctionDecl* matching_constructor_method_ast(const FunctionScope& scope,
                                                    const ClassDecl& klass,
                                                    const std::vector<Expr>& args,
                                                    const SourceLocation* location);
void check_constructor_args_ast(const FunctionScope& scope, const ClassDecl& klass,
                                const std::vector<Expr>& args, const SourceLocation* location);

} // namespace dudu
