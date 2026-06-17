#pragma once

#include "dudu/ast.hpp"
#include "dudu/cpp_emit_options.hpp"
#include "dudu/sema_context.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

std::optional<std::string> lower_swizzle_expr(const Expr& expr,
                                              const std::vector<std::string>& aliases,
                                              const std::map<std::string, std::string>& locals,
                                              const Symbols* symbols = nullptr);
std::optional<std::string> lower_swizzle_expr(const Expr& expr,
                                              const std::vector<std::string>& aliases,
                                              const std::map<std::string, std::string>& locals,
                                              const Symbols* symbols,
                                              const CppEmitOptions& options);
std::optional<std::string>
lower_swizzle_assignment(const Stmt& stmt, const std::vector<std::string>& aliases,
                         const std::map<std::string, std::string>& locals, const Symbols* symbols);
std::optional<std::string>
lower_swizzle_assignment(const Stmt& stmt, const std::vector<std::string>& aliases,
                         const std::map<std::string, std::string>& locals, const Symbols* symbols,
                         const CppEmitOptions& options);

} // namespace dudu
