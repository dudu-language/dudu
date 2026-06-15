#pragma once

#include "dudu/sema_context.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

bool type_derives_from(const Symbols& symbols, const std::string& derived, const std::string& base);
bool native_base_assignable(const Symbols& symbols, const std::string& expected,
                            const std::string& got);
bool class_type_has_instance_storage(const Symbols& symbols, const std::string& type);
std::vector<std::string> unimplemented_abstract_methods(const Symbols& symbols,
                                                        const std::string& type);
bool is_abstract_class_type(const Symbols& symbols, const std::string& type);
FunctionSignature method_signature_without_self(const FunctionDecl& method);
FunctionSignature inherited_method_signature_for_type(const ClassDecl& owner,
                                                      const std::string& receiver_type,
                                                      const FunctionDecl& method);
struct InheritedMethod {
    const FunctionDecl* method = nullptr;
    FunctionSignature signature;
};
std::optional<InheritedMethod> find_inherited_method(const Symbols& symbols,
                                                     const std::string& type,
                                                     const std::string& name);
const FunctionDecl* find_method_decl(const Symbols& symbols, const std::string& type,
                                     const std::string& name);
bool same_signature(const FunctionSignature& a, const FunctionSignature& b);
void check_multiple_inheritance_rules(const Symbols& symbols, const ClassDecl& klass);

} // namespace dudu
