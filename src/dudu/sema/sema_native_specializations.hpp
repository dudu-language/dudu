#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_generics_detail.hpp"

#include <map>
#include <optional>
#include <string>

namespace dudu {

TypeRef substitute_native_specialization_type(const TypeRef& type,
                                              const GenericTypeBindings& bindings);

const ClassDecl* native_specialized_class_for_owner(const Symbols& symbols, const TypeRef& owner,
                                                    GenericTypeBindings& bindings);

std::optional<TypeRef>
native_constexpr_value_ref(const Expr& expr, const std::map<std::string, TypeRef>& substitutions,
                           const SourceLocation& location);

} // namespace dudu
