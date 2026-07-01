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

std::string lower_slice_value_expr(const Expr& expr, const std::vector<std::string>& aliases,
                                   const CppLocalContext& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const Symbols* symbols, const CppEmitOptions& options);

std::optional<std::string> lower_generic_array_view_index_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options);

} // namespace dudu
