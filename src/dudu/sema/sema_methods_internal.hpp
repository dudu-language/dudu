#pragma once

#include "dudu/sema/sema_context.hpp"

#include <optional>
#include <string>

namespace dudu {

std::string receiver_class_name(const Symbols& symbols, const TypeRef& type);
TypeRef unwrap_receiver_type_ref(const Symbols& symbols, const TypeRef& type);
const ClassDecl* class_for_receiver_type(const Symbols& symbols, const TypeRef& type);
std::optional<TypeRef> field_type_ref_for_class(const Symbols& symbols, const ClassDecl& klass,
                                                const TypeRef& receiver_type,
                                                const std::string& field);

} // namespace dudu
