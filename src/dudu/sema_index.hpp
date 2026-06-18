#pragma once

#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dudu {

std::string indexed_type_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const std::string& type, const Expr& index_expr,
                                   const std::string& label);
TypeRef indexed_type_ref_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const TypeRef& type, const Expr& index_expr,
                                   const std::string& label);
TypeRef indexed_type_ref_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const std::string& type, const Expr& index_expr,
                                   const std::string& label);
TypeRef indexed_value_type_ref(const Symbols& symbols,
                               const std::map<std::string, TypeRef>& local_type_refs,
                               const SourceLocation& location, const std::string& name,
                               const Expr& index_expr, std::string_view unknown_message);
std::optional<TypeRef>
iterable_value_type_ref(const std::map<std::string, TypeRef>& local_type_refs,
                        const std::string& name);
std::optional<TypeRef> iterable_type_ref_from_type(TypeRef type);
void check_iterable_binding(const Symbols& symbols,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const SourceLocation& location, const TypeRef& binding_type,
                            const Expr& iterable);

} // namespace dudu
