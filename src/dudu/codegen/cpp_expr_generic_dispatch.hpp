#pragma once

#include "dudu/codegen/cpp_emit_context.hpp"
#include "dudu/codegen/cpp_emit_options.hpp"
#include "dudu/core/ast.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct Symbols;

std::optional<std::string>
lower_generic_method_dispatch(const Expr& expr, const std::vector<std::string>& aliases,
                              const CppLocalContext& locals,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const Symbols* symbols, const CppEmitOptions& options = {});

} // namespace dudu
