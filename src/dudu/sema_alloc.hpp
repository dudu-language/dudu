#pragma once

#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

std::optional<std::string> infer_allocation_call(const Symbols& symbols,
                                                 const SourceLocation* location,
                                                 const std::string& callee,
                                                 const std::vector<std::string>& args);

} // namespace dudu
