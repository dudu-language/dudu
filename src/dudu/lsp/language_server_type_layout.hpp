#pragma once

#include "dudu/core/ast.hpp"

#include <optional>
#include <string_view>

namespace dudu {

struct Symbols;

std::optional<TypeLayout> primitive_type_layout(std::string_view name);
std::optional<TypeLayout> resolved_type_layout(const Symbols& symbols, const TypeRef& type);
std::optional<TypeLayout> resolved_class_layout(const Symbols& symbols, const ClassDecl& klass);

} // namespace dudu
