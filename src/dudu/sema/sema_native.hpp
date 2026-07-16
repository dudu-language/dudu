#pragma once

#include "dudu/sema/sema_scope.hpp"
#include "dudu/core/source.hpp"

#include <optional>
#include <string>

namespace dudu {

bool foreign_cpp_type_name(const Symbols& symbols, const TypeRef& type);
bool foreign_cpp_class_type(const Symbols& symbols, const TypeRef& type);
bool is_native_path_prefix(const Symbols& symbols, const std::string& path);
std::optional<TypeRef> native_member_path_type_ref(const Symbols& symbols, const std::string& path,
                                                   SourceLocation location = {});
std::optional<TypeRef> native_member_expr_type_ref(const Symbols& symbols, const Expr& expr,
                                                   SourceLocation location = {});

} // namespace dudu
