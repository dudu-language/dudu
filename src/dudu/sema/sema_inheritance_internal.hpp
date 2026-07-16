#pragma once

#include "dudu/sema/sema_inheritance.hpp"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dudu::sema_inheritance_detail {

struct MethodIdentity {
    std::string name;
    FunctionSignature signature;
};

struct MethodRecord {
    MethodIdentity identity;
    std::string owner;
    std::string label;
};

std::string unwrap_type(const Symbols& symbols, const TypeRef& type);
bool derives_from_impl(const Symbols& symbols, const TypeRef& derived_type, const std::string& base,
                       std::set<std::string>& seen);
FunctionSignature inherited_method_signature_without_self(const FunctionDecl& method);
FunctionSignature inherited_method_signature_for_class_type(const ClassDecl& owner,
                                                            const TypeRef& receiver_type,
                                                            const FunctionDecl& method);
bool signatures_equivalent(const FunctionSignature& a, const FunctionSignature& b);
bool same_method_identity(const MethodIdentity& a, const MethodIdentity& b);
FunctionSignature resolve_signature_aliases(const Symbols& symbols, FunctionSignature signature);
bool contains_method_identity(const std::vector<MethodIdentity>& identities,
                              const MethodIdentity& candidate);
MethodRecord method_record_for_class_type(const Symbols& symbols, const ClassDecl& owner,
                                          const TypeRef& receiver_type, const FunctionDecl& method);
MethodIdentity method_identity_without_self(const Symbols& symbols, const FunctionDecl& method);
const ClassDecl* dudu_class_for_base(const Symbols& symbols, const TypeRef& base);
bool class_has_instance_storage(const Symbols& symbols, const ClassDecl& klass);
bool interface_like_base(const Symbols& symbols, const ClassDecl& klass);
void collect_inherited_fields(const Symbols& symbols, const ClassDecl& klass,
                              std::map<std::string, std::string>& fields,
                              std::set<std::string>& seen);
void collect_concrete_methods(const Symbols& symbols, const ClassDecl& klass,
                              const TypeRef& receiver_type, std::vector<MethodRecord>& methods,
                              std::set<std::string>& seen);
std::vector<MethodRecord> unresolved_abstract_method_records_impl(const Symbols& symbols,
                                                                  const TypeRef& type,
                                                                  std::set<std::string>& seen);

} // namespace dudu::sema_inheritance_detail
