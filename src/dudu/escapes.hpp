#pragma once

#include "dudu/ast.hpp"

#include <map>
#include <string>

namespace dudu {

void check_local_address_escape(const Stmt& stmt,
                                const std::map<std::string, TypeRef>& local_type_refs);

} // namespace dudu
