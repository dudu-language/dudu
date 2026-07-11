#pragma once

#include "dudu/core/ast.hpp"

#include <string>
#include <vector>

namespace dudu {

struct Symbols;

std::vector<TypeRef> template_arg_refs_from_type(const TypeRef& type);
TypeRef resolve_associated_type_ref(const Symbols& symbols, TypeRef type);
TypeRef substitute_receiver_template_type(const TypeRef& type, const ClassDecl& klass,
                                          const std::vector<TypeRef>& receiver_args);
TypeRef substitute_receiver_template_type(const TypeRef& type, const Symbols& symbols,
                                          const ClassDecl& klass,
                                          const std::vector<TypeRef>& receiver_args);

} // namespace dudu
