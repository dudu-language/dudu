#pragma once

#include "dudu/sema_context.hpp"

#include <map>
#include <string>

namespace dudu {

std::string member_path_type(const Symbols& symbols,
                             const std::map<std::string, std::string>& locals,
                             const SourceLocation* location, const std::string& path,
                             std::string unknown_local_prefix);

bool is_member_path(const std::string& path);

bool method_signature_for_type(const Symbols& symbols, std::string receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location);

} // namespace dudu
