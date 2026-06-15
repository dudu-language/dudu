#pragma once

#include "dudu/sema_context.hpp"

#include <string>
#include <vector>

namespace dudu {

bool type_derives_from(const Symbols& symbols, const std::string& derived,
                       const std::string& base);
bool native_base_assignable(const Symbols& symbols, const std::string& expected,
                            const std::string& got);
bool class_type_has_instance_storage(const Symbols& symbols, const std::string& type);
std::vector<std::string> unimplemented_abstract_methods(const Symbols& symbols,
                                                        const std::string& type);
bool is_abstract_class_type(const Symbols& symbols, const std::string& type);

} // namespace dudu
