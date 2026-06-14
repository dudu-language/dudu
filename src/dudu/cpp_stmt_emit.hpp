#pragma once

#include "dudu/ast.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals = {});

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases);
void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases,
                const std::map<std::string, std::string>& locals,
                const std::string& return_type = {},
                const std::map<std::string, std::string>& function_returns = {});

} // namespace dudu
