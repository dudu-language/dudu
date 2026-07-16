#pragma once

#include "dudu/codegen/cpp_emit_context.hpp"
#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

std::optional<std::string>
lower_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                            const CppLocalContext& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const std::map<std::string, TypeRef>& function_returns,
                            const Symbols* symbols, const CppEmitOptions& options);

std::optional<std::string>
lower_compound_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                                     const CppLocalContext& locals,
                                     const std::map<std::string, TypeRef>& local_type_refs,
                                     const std::map<std::string, TypeRef>& function_returns,
                                     const Symbols* symbols, const CppEmitOptions& options);

std::optional<std::string>
lower_index_read_hook(const Expr& expr, const std::vector<std::string>& aliases,
                      const CppLocalContext& locals,
                      const std::map<std::string, TypeRef>& local_type_refs, const Symbols* symbols,
                      const CppEmitOptions& options);

} // namespace dudu
