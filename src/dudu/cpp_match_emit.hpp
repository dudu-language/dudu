#pragma once

#include "dudu/ast.hpp"
#include "dudu/cpp_emit_context.hpp"
#include "dudu/cpp_emit_options.hpp"
#include "dudu/match_patterns.hpp"
#include "dudu/sema_context.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

bool match_has_guards(const Stmt& stmt);
bool match_cases_return(const Stmt& stmt);
void emit_match_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                          const std::vector<std::string>& aliases,
                          const CppLocalContext& locals,
                          const TypeRef& return_type_ref,
                          const std::map<std::string, TypeRef>& function_returns,
                          const Symbols* symbols);
void emit_match_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                          const std::vector<std::string>& aliases,
                          const CppLocalContext& locals,
                          const std::map<std::string, TypeRef>& local_type_refs,
                          const TypeRef& return_type_ref,
                          const std::map<std::string, TypeRef>& function_returns,
                          const Symbols* symbols, const CppEmitOptions& options);

} // namespace dudu
