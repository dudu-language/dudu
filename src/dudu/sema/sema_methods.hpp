#pragma once

#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct DuduMethodInstantiation {
    const ClassDecl* owner = nullptr;
    const EnumDecl* enum_owner = nullptr;
    const FunctionDecl* method = nullptr;
    TypeRef receiver_type;
    std::vector<TypeRef> receiver_args;
    std::vector<TypeRef> method_args;
    FunctionSignature signature;
};

std::optional<TypeRef> static_class_receiver_type_ref(const FunctionScope& scope,
                                                      const Expr& receiver);

TypeRef member_expr_type_ref(const Symbols& symbols,
                             const std::map<std::string, TypeRef>& local_type_refs,
                             const SourceLocation* location, const Expr& expr,
                             std::string_view unknown_local_prefix = {},
                             std::string_view current_class = {});

std::optional<TypeRef> field_type_ref_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                                               const std::string& field);
std::optional<TypeRef> swizzle_type_ref_for_type(const Symbols& symbols,
                                                 const TypeRef& receiver_type,
                                                 const std::string& swizzle);
std::optional<TypeRef> swizzle_assignment_type_ref_for_type(const Symbols& symbols,
                                                            const SourceLocation& location,
                                                            const TypeRef& receiver_type,
                                                            const std::string& swizzle);

bool method_signature_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location);
bool method_signature_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                               const std::string& method_name,
                               const std::vector<TypeRef>& method_args,
                               FunctionSignature& signature, const SourceLocation* location);
std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location);
std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const std::optional<TypeRef>& expected_return,
    const SourceLocation* location);
std::vector<DuduMethodInstantiation>
dudu_method_instantiations_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                                    const std::string& method_name,
                                    const std::vector<TypeRef>& method_args);
std::vector<DuduMethodInstantiation>
dudu_static_method_instantiations_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                                           const std::string& method_name,
                                           const std::vector<TypeRef>& method_args);
std::optional<DuduMethodInstantiation> inferred_dudu_method_instantiation_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location);
std::optional<DuduMethodInstantiation> inferred_dudu_method_instantiation_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const std::optional<TypeRef>& expected_return,
    const SourceLocation* location);
std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          const TypeRef& receiver_type,
                                                          const std::string& method_name);
std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          const TypeRef& receiver_type,
                                                          const std::string& method_name,
                                                          const std::vector<TypeRef>& method_args);
bool static_method_signature_for_type(const Symbols& symbols, const TypeRef& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location);
bool static_method_signature_for_type(const Symbols& symbols, const TypeRef& type_name,
                                      const std::string& method_name,
                                      const std::vector<TypeRef>& method_args,
                                      FunctionSignature& signature, const SourceLocation* location);

} // namespace dudu
