#pragma once

#include "dudu/sema_context.hpp"

#include <string>

namespace dudu {

bool type_derives_from(const Symbols& symbols, const std::string& derived,
                       const std::string& base);
bool native_base_assignable(const Symbols& symbols, const std::string& expected,
                            const std::string& got);

} // namespace dudu
