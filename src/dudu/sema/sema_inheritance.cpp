#include "dudu/sema/sema_inheritance.hpp"

#include "dudu/sema/sema_inheritance_internal.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace dudu {
using namespace sema_inheritance_detail;

bool native_base_assignable(const Symbols& symbols, const TypeRef& expected, const TypeRef& got) {
    const std::string base = unwrap_type(symbols, expected);
    const std::string derived = unwrap_type(symbols, got);
    std::set<std::string> seen;
    return base != derived && derives_from_impl(symbols, got, base, seen);
}

bool class_type_has_instance_storage(const Symbols& symbols, const TypeRef& type) {
    const ClassDecl* klass = dudu_class_for_base(symbols, type);
    if (klass == nullptr) {
        return false;
    }
    return !klass->fields.empty() ||
           std::any_of(klass->base_class_refs.begin(), klass->base_class_refs.end(),
                       [&](const BaseClassDecl& base) {
                           return class_type_has_instance_storage(symbols, base.type_ref);
                       });
}

bool same_signature(const FunctionSignature& a, const FunctionSignature& b) {
    return signatures_equivalent(a, b);
}

std::vector<std::string> unimplemented_abstract_methods(const Symbols& symbols,
                                                        const TypeRef& type) {
    std::set<std::string> seen;
    const std::vector<MethodRecord> records =
        unresolved_abstract_method_records_impl(symbols, type, seen);
    std::vector<std::string> out;
    out.reserve(records.size());
    for (const MethodRecord& record : records) {
        out.push_back(record.label);
    }
    return out;
}

FunctionSignature method_signature_without_self(const FunctionDecl& method) {
    return inherited_method_signature_without_self(method);
}

FunctionSignature inherited_method_signature_for_type(const ClassDecl& owner,
                                                      const TypeRef& receiver_type,
                                                      const FunctionDecl& method) {
    return inherited_method_signature_for_class_type(owner, receiver_type, method);
}

std::optional<InheritedMethod> find_inherited_method(const Symbols& symbols, const TypeRef& type,
                                                     const std::string& name) {
    const ClassDecl* klass = dudu_class_for_base(symbols, type);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (method.name == name) {
            return InheritedMethod{
                .method = &method,
                .signature = inherited_method_signature_for_class_type(*klass, type, method),
            };
        }
    }
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        if (auto method = find_inherited_method(symbols, base_decl.type_ref, name)) {
            return method;
        }
    }
    return std::nullopt;
}

const FunctionDecl* find_method_decl(const Symbols& symbols, const TypeRef& type,
                                     const std::string& name) {
    const ClassDecl* klass = dudu_class_for_base(symbols, type);
    if (klass == nullptr) {
        return nullptr;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (method.name == name) {
            return &method;
        }
    }
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        if (const FunctionDecl* method = find_method_decl(symbols, base_decl.type_ref, name)) {
            return method;
        }
    }
    return nullptr;
}

void check_multiple_inheritance_rules(const Symbols& symbols, const ClassDecl& klass) {
    if (klass.base_class_refs.size() <= 1) {
        return;
    }
    size_t storage_bases = 0;
    std::map<std::string, std::string> inherited_fields;
    std::vector<MethodRecord> inherited_concrete_methods;
    std::vector<MethodIdentity> derived_overrides;
    for (const FunctionDecl& method : klass.methods) {
        if (has_decorator(method, "override")) {
            derived_overrides.push_back(method_identity_without_self(symbols, method));
        }
    }

    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        const ClassDecl* parent = dudu_class_for_base(symbols, base_decl.type_ref);
        if (parent == nullptr) {
            continue;
        }
        const bool has_storage = class_has_instance_storage(symbols, *parent);
        if (has_storage) {
            ++storage_bases;
            if (storage_bases > 1) {
                throw CompileError(
                    klass.location,
                    "multiple inheritance allows at most one storage-bearing Dudu base");
            }
        } else if (!interface_like_base(symbols, *parent)) {
            throw CompileError(
                klass.location,
                "multiple inheritance non-storage bases must be abstract interface-like classes");
        }

        std::map<std::string, std::string> fields;
        std::set<std::string> field_seen;
        collect_inherited_fields(symbols, *parent, fields, field_seen);
        for (const auto& [name, owner] : fields) {
            if (const auto existing = inherited_fields.find(name);
                existing != inherited_fields.end() && existing->second != owner) {
                throw CompileError(klass.location, "duplicate inherited field: " + name);
            }
            inherited_fields.emplace(name, owner);
        }

        std::vector<MethodRecord> methods;
        std::set<std::string> method_seen;
        collect_concrete_methods(symbols, *parent, base_decl.type_ref, methods, method_seen);
        for (const MethodRecord& method : methods) {
            const auto existing =
                std::find_if(inherited_concrete_methods.begin(), inherited_concrete_methods.end(),
                             [&](const MethodRecord& entry) {
                                 return same_method_identity(entry.identity, method.identity);
                             });
            if (existing != inherited_concrete_methods.end() && existing->owner != method.owner &&
                !contains_method_identity(derived_overrides, method.identity)) {
                throw CompileError(klass.location,
                                   "ambiguous inherited concrete method: " + method.label);
            }
            if (existing == inherited_concrete_methods.end()) {
                inherited_concrete_methods.push_back(method);
            }
        }
    }
}

} // namespace dudu
