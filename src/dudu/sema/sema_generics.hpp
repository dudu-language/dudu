#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dudu {

bool generic_param_is_pack(std::string_view param);
std::string generic_param_base_name(std::string_view param);
bool generic_pack_param_named(const std::vector<std::string>& params, std::string_view name);
bool generic_arity_matches(const std::vector<std::string>& params, size_t arg_count);
size_t generic_min_arity(const std::vector<std::string>& params);
bool generic_decl_arity_matches(const std::vector<std::string>& params,
                                std::optional<size_t> minimum, size_t arg_count);
std::vector<TypeRef> generic_args_with_defaults(const std::vector<std::string>& params,
                                                const std::vector<TypeRef>& defaults,
                                                const std::vector<TypeRef>& args);
size_t generic_decl_min_arity(const std::vector<std::string>& params,
                              std::optional<size_t> minimum);
bool class_generic_arity_matches(const ClassDecl& klass, size_t arg_count);
size_t class_generic_min_arity(const ClassDecl& klass);
TypeRef substitute_generic_type_ref(const std::vector<std::string>& params,
                                    const std::vector<TypeRef>& args, const TypeRef& type);
std::vector<TypeRef> template_type_refs(const Expr& expr);
std::set<std::string> generic_value_params(const std::vector<std::string>& params,
                                           const std::vector<TypeRef>& type_refs);
std::set<std::string> generic_cpp_value_params(const std::vector<std::string>& params,
                                               const std::vector<TypeRef>& type_refs);
std::vector<std::string> generic_cpp_params(const std::vector<std::string>& params,
                                            const std::set<std::string>& semantic_value_params,
                                            const std::set<std::string>& cpp_value_params);
std::set<std::string> generic_value_params_for_function(const FunctionDecl& fn);
std::set<std::string> generic_value_params_for_class(const ClassDecl& klass);
std::set<std::string> generic_cpp_value_params_for_function(const FunctionDecl& fn);
std::set<std::string> generic_cpp_value_params_for_class(const ClassDecl& klass);
std::vector<std::string> generic_cpp_params_for_function(const FunctionDecl& fn);
std::vector<std::string> generic_cpp_params_for_class(const ClassDecl& klass);
std::optional<std::vector<TypeRef>> infer_generic_call_type_args(const FunctionScope& scope,
                                                                 const FunctionDecl& fn,
                                                                 const std::string& callee,
                                                                 const std::vector<Expr>& args,
                                                                 const SourceLocation* location);
std::optional<std::vector<TypeRef>>
infer_generic_method_type_args(const FunctionScope& scope, const FunctionDecl& method,
                               const std::string& callee, const std::vector<Expr>& args,
                               size_t first_param, const SourceLocation* location,
                               const TypeRef* receiver_type = nullptr);
std::optional<std::vector<TypeRef>> infer_generic_method_type_args_from_type_refs(
    const FunctionDecl& method, const std::string& callee, const std::vector<TypeRef>& arg_types,
    size_t first_param, const std::optional<TypeRef>& expected_return,
    const SourceLocation* location, const TypeRef* receiver_type = nullptr);
FunctionSignature instantiate_generic_signature(const FunctionDecl& fn,
                                                const std::vector<TypeRef>& args);
ClassDecl instantiate_generic_class(ClassDecl klass, const std::vector<TypeRef>& args,
                                    const std::string& instantiated_name);
bool known_template_constructor_type(const FunctionScope& scope, const TypeRef& callee_type);

} // namespace dudu
