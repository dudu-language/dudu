#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_generics_detail.hpp"

#include <string>
#include <vector>

namespace dudu {

struct Symbols;

std::vector<TypeRef> template_arg_refs_from_type(const TypeRef& type);
TypeRef resolve_associated_type_ref(const Symbols& symbols, TypeRef type);
TypeRef structure_owner_alias_templates(TypeRef type, const ClassDecl& owner_class,
                                        const TypeRef& owner);
GenericTypeBindings specialized_owner_member_bindings(
    const ClassDecl& owner_class, const TypeRef& owner,
    const GenericTypeBindings& specialization_bindings);
TypeRef substitute_receiver_template_type(const TypeRef& type, const ClassDecl& klass,
                                          const std::vector<TypeRef>& receiver_args);
TypeRef substitute_receiver_template_type(const TypeRef& type, const Symbols& symbols,
                                          const ClassDecl& klass,
                                          const std::vector<TypeRef>& receiver_args);

} // namespace dudu
