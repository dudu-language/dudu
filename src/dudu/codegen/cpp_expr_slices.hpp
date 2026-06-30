#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/codegen/cpp_emit_context.hpp"
#include "dudu/codegen/cpp_emit_options.hpp"
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

std::optional<std::string> lower_trailing_full_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options);
std::optional<std::string> lower_trailing_range_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options);
std::optional<std::string>
lower_column_slice_expr(const Expr& base, const Expr& index,
                        const std::vector<std::string>& aliases, const CppLocalContext& locals,
                        const std::map<std::string, TypeRef>& local_type_refs,
                        const Symbols* symbols, const CppEmitOptions& options);
std::optional<std::string>
lower_channel_slice_expr(const Expr& base, const Expr& index,
                         const std::vector<std::string>& aliases, const CppLocalContext& locals,
                         const std::map<std::string, TypeRef>& local_type_refs,
                         const Symbols* symbols, const CppEmitOptions& options);

std::optional<std::string> lower_leading_range_full_tail_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options);

std::optional<std::string> lower_matrix_patch_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options);

std::optional<std::string> lower_full_multidim_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options);

std::optional<std::string> lower_strided_span2_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options);

} // namespace dudu
