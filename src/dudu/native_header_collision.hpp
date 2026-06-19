#pragma once

#include "dudu/source.hpp"

#include <string_view>

namespace dudu {

bool actionable_native_name_collision(std::string_view name);
bool non_actionable_native_collision_location(const SourceLocation& location);
bool native_decl_collision_is_error(std::string_view name, const SourceLocation& location);

} // namespace dudu
