#pragma once

#include "dudu/sema/sema_context.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

bool native_base_assignable(const Symbols& symbols, const TypeRef& expected, const TypeRef& got);
bool class_type_has_instance_storage(const Symbols& symbols, const TypeRef& type);
std::vector<std::string> unimplemented_abstract_methods(const Symbols& symbols,
                                                        const TypeRef& type);
FunctionSignature method_signature_without_self(const FunctionDecl& method);
FunctionSignature inherited_method_signature_for_type(const ClassDecl& owner,
                                                      const TypeRef& receiver_type,
                                                      const FunctionDecl& method);
struct InheritedMethod {
    const FunctionDecl* method = nullptr;
    FunctionSignature signature;
};
std::optional<InheritedMethod> find_inherited_method(const Symbols& symbols, const TypeRef& type,
                                                     const std::string& name);
const FunctionDecl* find_method_decl(const Symbols& symbols, const TypeRef& type,
                                     const std::string& name);
bool same_signature(const FunctionSignature& a, const FunctionSignature& b);
void check_multiple_inheritance_rules(const Symbols& symbols, const ClassDecl& klass);

} // namespace dudu
