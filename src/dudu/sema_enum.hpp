#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"

#include <optional>
#include <string>
#include <utility>

namespace dudu {

const EnumDecl* enum_decl_for_type(const Symbols& symbols, const TypeRef& type);
const EnumDecl* enum_decl_for_type(const Symbols& symbols, const std::string& type);
const EnumValueDecl* enum_variant_decl(const EnumDecl& en, const std::string& variant);
std::optional<std::pair<const EnumDecl*, const EnumValueDecl*>>
enum_variant_from_path(const Symbols& symbols, const std::string& path);
std::optional<std::pair<const EnumDecl*, const EnumValueDecl*>>
enum_variant_from_expr(const Symbols& symbols, const Expr& expr);

} // namespace dudu
