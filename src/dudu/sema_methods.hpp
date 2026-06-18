#pragma once

#include "dudu/sema_context.hpp"
#include "dudu/sema_generics.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

std::string member_expr_type(const Symbols& symbols,
                             const std::map<std::string, TypeRef>& local_type_refs,
                             const SourceLocation* location, const Expr& expr,
                             std::string_view unknown_local_prefix = {},
                             std::string_view current_class = {});
TypeRef member_expr_type_ref(const Symbols& symbols,
                             const std::map<std::string, TypeRef>& local_type_refs,
                             const SourceLocation* location, const Expr& expr,
                             std::string_view unknown_local_prefix = {},
                             std::string_view current_class = {});

std::optional<std::string> field_type_for_type(const Symbols& symbols,
                                               const std::string& receiver_type,
                                               const std::string& field);
std::optional<TypeRef> field_type_ref_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                                               const std::string& field);
std::optional<std::string> swizzle_type_for_type(const Symbols& symbols,
                                                 const std::string& receiver_type,
                                                 const std::string& swizzle);
std::optional<TypeRef> swizzle_type_ref_for_type(const Symbols& symbols,
                                                 const TypeRef& receiver_type,
                                                 const std::string& swizzle);
std::optional<std::string> swizzle_assignment_type_for_type(const Symbols& symbols,
                                                            const SourceLocation& location,
                                                            const std::string& receiver_type,
                                                            const std::string& swizzle);
std::optional<TypeRef> swizzle_assignment_type_ref_for_type(const Symbols& symbols,
                                                            const SourceLocation& location,
                                                            const TypeRef& receiver_type,
                                                            const std::string& swizzle);

bool method_signature_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location);
bool method_signature_for_type(const Symbols& symbols, std::string receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location);
std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location,
    const GenericInferCallbacks& callbacks);
std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, std::string receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location,
    const GenericInferCallbacks& callbacks);
std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const std::string& expected_return,
    const SourceLocation* location, const GenericInferCallbacks& callbacks);
std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, std::string receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const std::string& expected_return,
    const SourceLocation* location, const GenericInferCallbacks& callbacks);
std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          const TypeRef& receiver_type,
                                                          const std::string& method_name);
std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          std::string receiver_type,
                                                          const std::string& method_name);
bool static_method_signature_for_type(const Symbols& symbols, const TypeRef& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location);
bool static_method_signature_for_type(const Symbols& symbols, const std::string& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location);

} // namespace dudu
