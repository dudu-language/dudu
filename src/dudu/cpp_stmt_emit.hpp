#pragma once

#include "dudu/ast.hpp"
#include "dudu/cpp_emit_options.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

struct Symbols;

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases);
void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases,
                const std::map<std::string, std::string>& locals,
                const TypeRef& return_type_ref = {},
                const std::map<std::string, TypeRef>& function_returns = {},
                const Symbols* symbols = nullptr);
void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases,
                const std::map<std::string, std::string>& locals, const TypeRef& return_type_ref,
                const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols,
                const CppEmitOptions& options);
void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases,
                const std::map<std::string, std::string>& locals,
                const std::map<std::string, TypeRef>& local_type_refs,
                const TypeRef& return_type_ref,
                const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols,
                const CppEmitOptions& options);

} // namespace dudu
