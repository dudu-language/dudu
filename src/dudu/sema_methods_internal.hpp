#pragma once

#include "dudu/sema_context.hpp"

#include <optional>
#include <string>

namespace dudu {

std::string unwrap_receiver_type(const Symbols& symbols, std::string type);
std::optional<std::string> field_type_for_class(const Symbols& symbols, const ClassDecl& klass,
                                                const std::string& receiver_type,
                                                const std::string& field);

} // namespace dudu
