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

} // namespace dudu
