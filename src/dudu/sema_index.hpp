#pragma once

#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"

#include <map>
#include <string>
#include <string_view>

namespace dudu {

std::string indexed_value_type(const Symbols& symbols,
                               const std::map<std::string, std::string>& locals,
                               const SourceLocation& location, const std::string& name,
                               std::string_view unknown_message);

} // namespace dudu
