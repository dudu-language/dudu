#pragma once

#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace dudu {

struct Symbols;

void emit_class_method_members(std::ostringstream& out, const ClassDecl& klass,
                               const std::string& class_name,
                               const std::vector<std::string>& aliases,
                               const std::map<std::string, TypeRef>& function_returns,
                               const Symbols& symbols, const CppEmitOptions& options);

void emit_out_of_line_class_methods(std::ostringstream& out, const ClassDecl& klass,
                                    const std::string& class_name,
                                    const std::vector<std::string>& aliases,
                                    const std::map<std::string, TypeRef>& function_returns,
                                    const Symbols& symbols, const CppEmitOptions& options);

} // namespace dudu
