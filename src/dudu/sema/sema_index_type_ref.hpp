#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/source.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace dudu {

struct Symbols;

std::optional<TypeRef> indexed_type_ref_from_type_ref_with_count(const Symbols& symbols,
                                                                 const SourceLocation& location,
                                                                 const TypeRef& receiver_type,
                                                                 size_t index_count, bool is_slice,
                                                                 bool has_step,
                                                                 const std::string& label);
TypeRef array_element_template_type_ref(const SourceLocation& location, const TypeRef& array_type,
                                        std::string_view template_name);

} // namespace dudu
