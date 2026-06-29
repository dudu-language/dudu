#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/codegen/cpp_emit_context.hpp"
#include "dudu/codegen/cpp_emit_options.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct Symbols;

std::optional<std::string>
lower_expected_generic_method_call(const TypeRef& expected_type, const Expr& expr,
                                   const std::vector<std::string>& aliases,
                                   const CppLocalContext& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const std::map<std::string, TypeRef>& function_returns,
                                   const Symbols* symbols, const CppEmitOptions& options);

} // namespace dudu
