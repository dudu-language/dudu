#pragma once

#include "dudu/sema_expr_internal.hpp"

#include <optional>
#include <string>

namespace dudu::cpp_escape_infer_detail {

TypeRef cpp_escape_member_path_type_ref(const FunctionScope& scope, const SourceLocation* location,
                                        const std::string& path);
TypeRef cpp_escape_member_expr_type_ref(const FunctionScope& scope, const SourceLocation* location,
                                        const Expr& expr);
bool known_cpp_escape_type_ref(const Symbols& symbols, const TypeRef& type);
std::optional<TypeRef> infer_parsed_index_escape_ref(const FunctionScope& scope, const Expr& parsed,
                                                     const SourceLocation* location);
std::optional<TypeRef> infer_parsed_pointer_cast_escape_ref(const FunctionScope& scope,
                                                            const Expr& parsed,
                                                            const SourceLocation* location);
std::optional<TypeRef> infer_parsed_unary_escape_ref(const FunctionScope& scope, const Expr& parsed,
                                                     const SourceLocation* location);
std::optional<TypeRef> infer_parsed_template_escape_ref(const FunctionScope& scope,
                                                        const Expr& parsed,
                                                        const SourceLocation* location);
std::optional<TypeRef> infer_parsed_name_escape_ref(const FunctionScope& scope, const Expr& parsed,
                                                    const SourceLocation* location);

} // namespace dudu::cpp_escape_infer_detail
