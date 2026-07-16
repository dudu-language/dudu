#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"

#include <optional>
#include <string>
#include <utility>

namespace dudu {

const EnumDecl* enum_decl_for_type(const Symbols& symbols, const TypeRef& type);
const EnumValueDecl* enum_variant_decl(const EnumDecl& en, const std::string& variant);
bool enum_has_payloads(const EnumDecl& en);
std::optional<std::pair<const EnumDecl*, const EnumValueDecl*>>
enum_variant_from_path(const Symbols& symbols, const std::string& path);
std::optional<std::pair<const EnumDecl*, const EnumValueDecl*>>
enum_variant_from_expr(const Symbols& symbols, const Expr& expr);

} // namespace dudu
