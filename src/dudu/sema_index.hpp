#pragma once

#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"

#include <map>
#include <string>
#include <string_view>

namespace dudu {

std::string indexed_type_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const std::string& type, const std::string& index_expr,
                                   const std::string& label);
std::string indexed_type_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const std::string& type, const Expr& index_expr,
                                   const std::string& label);
std::string indexed_value_type(const Symbols& symbols,
                               const std::map<std::string, std::string>& locals,
                               const SourceLocation& location, const std::string& name,
                               const Expr& index_expr, std::string_view unknown_message);
std::string iterable_value_type(const Symbols& symbols,
                                const std::map<std::string, std::string>& locals,
                                const std::string& name);
void check_iterable_binding(const Symbols& symbols,
                            const std::map<std::string, std::string>& locals,
                            const SourceLocation& location, const TypeRef& binding_type,
                            const Expr& iterable);

} // namespace dudu
