#pragma once

#include "dudu/core/ast.hpp"

#include <vector>

namespace dudu {

bool block_guarantees_return(const std::vector<Stmt>& body);

} // namespace dudu
