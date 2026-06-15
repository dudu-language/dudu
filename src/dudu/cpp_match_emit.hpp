#pragma once

#include "dudu/ast.hpp"
#include "dudu/match_patterns.hpp"
#include "dudu/sema_context.hpp"

namespace dudu {

bool match_has_guards(const Stmt& stmt);
bool match_cases_return(const Stmt& stmt);

} // namespace dudu
