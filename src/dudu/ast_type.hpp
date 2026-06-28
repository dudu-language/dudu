#pragma once

#include "dudu/ast.hpp"

#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

std::vector<TypeRef> template_type_arg_refs(const TypeRef& type, std::string_view name);
std::vector<TypeRef>
template_type_arg_refs_with_aliases(const TypeRef& type, std::string_view name,
                                    const std::map<std::string, TypeRef>& aliases);
std::optional<TypeRef> unary_type_child_ref(const TypeRef& type, TypeKind kind);
std::optional<TypeRef> unary_type_child_ref(const TypeRef& type,
                                            std::initializer_list<TypeKind> kinds);
bool type_ref_contains_kind(const TypeRef& type, TypeKind kind);
std::string type_ref_head_name(const TypeRef& type);
std::string type_ref_text(const TypeRef& type);
bool has_type_ref(const TypeRef& type);
bool has_expr_type_ref(const Expr& expr);
const TypeRef& expr_type_ref(const Expr& expr);
void set_expr_type_ref(Expr& expr, TypeRef type);
bool has_stmt_type_ref(const Stmt& stmt);
const TypeRef& stmt_type_ref(const Stmt& stmt);
void set_stmt_type_ref(Stmt& stmt, TypeRef type);
bool type_ref_is_name(const TypeRef& type, std::string_view name);
bool type_ref_is_auto(const TypeRef& type);
bool type_ref_is_void(const TypeRef& type);
bool type_ref_same_shape(const TypeRef& left, const TypeRef& right);
bool type_ref_equivalent(const TypeRef& left, const TypeRef& right);
TypeRef named_type_ref(std::string name, SourceLocation location = {});
TypeRef void_type_ref(SourceLocation location = {});
TypeRef wrapped_type_ref(TypeKind kind, TypeRef child, SourceLocation location = {});
TypeRef pack_expansion_type_ref(TypeRef child, SourceLocation location = {});
bool function_has_receiver_type(const FunctionDecl& fn);
bool function_has_return_type(const FunctionDecl& fn);
TypeRef function_return_type_ref(const FunctionDecl& fn);
std::vector<TypeRef> native_function_param_type_refs(const NativeFunctionDecl& fn);
TypeRef native_function_return_type_ref(const NativeFunctionDecl& fn);
TypeRef native_type_alias_type_ref(const NativeTypeDecl& type);
TypeRef native_value_type_ref(const NativeValueDecl& value);
std::string native_type_alias_type_text(const NativeTypeDecl& type);
std::string native_value_type_text(const NativeValueDecl& value);
TypeRef substitute_type_ref(const TypeRef& type,
                            const std::map<std::string, TypeRef>& substitutions);
std::string substitute_type_ref_text(const TypeRef& type,
                                     const std::map<std::string, std::string>& substitutions);

} // namespace dudu
