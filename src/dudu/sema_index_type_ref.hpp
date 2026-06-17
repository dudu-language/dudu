#pragma once

#include "dudu/ast.hpp"
#include "dudu/source.hpp"

#include <optional>
#include <string>

namespace dudu {

std::optional<TypeRef> indexed_type_ref_from_type_ref_with_count(const SourceLocation& location,
                                                                 const TypeRef& raw_type,
                                                                 size_t index_count, bool is_slice,
                                                                 bool has_step,
                                                                 const std::string& label);

} // namespace dudu
