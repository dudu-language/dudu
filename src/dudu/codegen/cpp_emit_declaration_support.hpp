#pragma once

#include "dudu/core/ast.hpp"

#include <iosfwd>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

bool visible_in_cpp_header(Visibility visibility);

void emit_cpp_template_parameters(std::ostringstream& out, const std::vector<std::string>& params,
                                  const std::set<std::string>& value_params = {},
                                  std::string_view prefix = {});

} // namespace dudu
