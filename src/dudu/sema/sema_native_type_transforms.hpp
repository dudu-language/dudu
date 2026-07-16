#pragma once

#include "dudu/core/ast.hpp"

namespace dudu {

struct Symbols;

TypeRef resolve_native_type_transform(const Symbols& symbols, TypeRef type);

} // namespace dudu
