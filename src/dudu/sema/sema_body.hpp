#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_scope.hpp"

namespace dudu {

void check_bodies(const ModuleAst& module, const Symbols& symbols);
void check_instantiated_generic_function_body(const FunctionScope& caller_scope,
                                              const FunctionDecl& fn,
                                              const std::vector<TypeRef>& type_args,
                                              const std::string& label,
                                              const SourceLocation& instantiation_site);
void check_instantiated_imported_generic_function_body(
    const FunctionScope& caller_scope, const std::string& callee, const std::vector<Expr>& args,
    const std::optional<std::vector<TypeRef>>& explicit_type_args,
    const SourceLocation& instantiation_site);
void check_instantiated_generic_method_body(const FunctionScope& caller_scope,
                                            const ClassDecl& owner, const FunctionDecl& method,
                                            const TypeRef& receiver_type,
                                            const std::vector<TypeRef>& receiver_args,
                                            const std::vector<TypeRef>& method_args,
                                            const SourceLocation& instantiation_site);

} // namespace dudu
