#pragma once

#include "dudu/ast.hpp"

#include <map>
#include <string>

namespace dudu {

std::string rewrite_pointer_members(std::string expr,
                                    const std::map<std::string, TypeRef>& local_type_refs);

} // namespace dudu
