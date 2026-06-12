#pragma once

#include "dudu/ast.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace dudu {

void emit_raw_block(std::ostringstream& out, const std::vector<RawStmt>& body, int depth,
                    const std::vector<std::string>& aliases);

} // namespace dudu
