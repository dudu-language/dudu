#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_scope.hpp"

#include <functional>
#include <string>
#include <vector>

namespace dudu {

struct MatchCheckCallbacks {
    std::function<std::string(FunctionScope&, const Expr&, const SourceLocation*)> infer_expr;
    std::function<void(FunctionScope&, const std::vector<Stmt>&, const std::string&, int)> check_block;
};

void check_match_stmt(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                      int loop_depth, const MatchCheckCallbacks& callbacks);

} // namespace dudu
