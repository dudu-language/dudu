#pragma once

#include "dudu/sema_context.hpp"

#include <string>

namespace dudu {

TypeRef receiver_template_type_ref(const Symbols& symbols, TypeRef type);
std::string receiver_template_type(const Symbols& symbols, std::string type);
bool builtin_cpp_method_signature(const Symbols& symbols, std::string receiver_type,
                                  const std::string& method_name, FunctionSignature& signature);

} // namespace dudu
