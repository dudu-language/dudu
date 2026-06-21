#pragma once

#include "dudu/array_shape.hpp"
#include "dudu/ast.hpp"
#include "dudu/cpp_emit_context.hpp"
#include "dudu/cpp_emit_options.hpp"
#include "dudu/sema_context.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

struct EffectiveStmtType {
    TypeRef ref;
};

std::string join_names(const std::vector<std::string>& names);
std::string_view compound_assign_op_text(CompoundAssignOp op);
EffectiveStmtType effective_stmt_type(const Stmt& stmt, const ArrayShapeInference& inferred);
std::string lower_declared_stmt_type(const TypeRef& type, const std::vector<std::string>& aliases,
                                     const CppEmitOptions& options);
std::string lower_emitted_expr(const Expr& expr, const std::vector<std::string>& aliases,
                               const CppLocalContext& locals,
                               const std::map<std::string, TypeRef>& local_type_refs,
                               const Symbols* symbols, const CppEmitOptions& options);
std::string lower_expr_as_type_ref(const TypeRef& expected_type, const Expr& expr,
                                   const std::vector<std::string>& aliases,
                                   const CppLocalContext& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const std::map<std::string, TypeRef>& function_returns,
                                   const Symbols* symbols, const CppEmitOptions& options);
std::string lower_fixed_array_literal_as_type_ref(
    const TypeRef& expected_type, const Expr& expr, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols,
    const CppEmitOptions& options);
bool is_template_type(const TypeRef& type, std::string_view name);
TypeRef emitted_local_type_ref(const std::map<std::string, TypeRef>& local_type_refs,
                               std::string_view name, SourceLocation location);
bool is_fixed_array_type(const TypeRef& type);

} // namespace dudu
