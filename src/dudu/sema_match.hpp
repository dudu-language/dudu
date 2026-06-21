#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_scope.hpp"

#include <string>
#include <vector>

namespace dudu {

using MatchBlockChecker = void (*)(FunctionScope&, const std::vector<Stmt>&, const TypeRef&, int);

struct MatchCheckContext {
    MatchBlockChecker check_block = nullptr;
};

void check_match_stmt(FunctionScope& scope, const Stmt& stmt, const TypeRef& return_type,
                      int loop_depth, MatchCheckContext context);

} // namespace dudu
