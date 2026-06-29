#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/source.hpp"

#include <map>
#include <string>
#include <vector>

namespace dudu {

void check_local_binding_name(const SourceLocation& location, const std::string& name);
void check_destructure_bindings(const SourceLocation& location,
                                const std::vector<std::string>& names,
                                const std::map<std::string, TypeRef>& local_type_refs);

} // namespace dudu
