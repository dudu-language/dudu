#pragma once

#include "dudu/ast.hpp"
#include "dudu/cpp_emit_context.hpp"
#include "dudu/cpp_emit_options.hpp"
#include "dudu/sema_context.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

std::optional<std::string>
lower_trailing_full_slice_expr(const Expr& base, const Expr& index,
                               const std::vector<std::string>& aliases,
                               const CppLocalContext& locals,
                               const std::map<std::string, TypeRef>& local_type_refs,
                               const Symbols* symbols, const CppEmitOptions& options);
std::optional<std::string>
lower_column_slice_expr(const Expr& base, const Expr& index,
                        const std::vector<std::string>& aliases,
                        const CppLocalContext& locals,
                        const std::map<std::string, TypeRef>& local_type_refs,
                        const Symbols* symbols, const CppEmitOptions& options);
std::optional<std::string>
lower_channel_slice_expr(const Expr& base, const Expr& index,
                         const std::vector<std::string>& aliases,
                         const CppLocalContext& locals,
                         const std::map<std::string, TypeRef>& local_type_refs,
                         const Symbols* symbols, const CppEmitOptions& options);

} // namespace dudu
