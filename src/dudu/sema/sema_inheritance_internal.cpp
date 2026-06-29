#include "dudu/sema/sema_inheritance_internal.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <algorithm>
#include <sstream>

namespace dudu::sema_inheritance_detail {

std::string unwrap_type(const Symbols& symbols, const TypeRef& type) {
    return receiver_class_name(symbols, type);
}

bool ref_like(const TypeRef& type) {
    return type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference;
}

bool derives_from_impl(const Symbols& symbols, const TypeRef& derived_type,
                       const std::string& base, std::set<std::string>& seen) {
    const std::string derived = unwrap_type(symbols, derived_type);
    if (derived == base)
        return true;
    if (!seen.insert(derived).second)
        return false;
    const ClassDecl* klass = class_for_receiver_type(symbols, derived_type);
    if (klass == nullptr)
        return false;
    for (const BaseClassDecl& parent : klass->base_class_refs) {
        if (derives_from_impl(symbols, parent.type_ref, base, seen))
            return true;
    }
    return false;
}

bool has_decorator(const FunctionDecl& fn, std::string_view name) {
    return dudu::has_decorator(fn.decorators, name);
}

std::map<std::string, TypeRef> class_type_ref_substitutions(const ClassDecl& owner,
                                                            const std::vector<TypeRef>& args) {
    std::map<std::string, TypeRef> substitutions;
    for (size_t i = 0; i < owner.generic_params.size() && i < args.size(); ++i) {
        substitutions.emplace(owner.generic_params[i], args[i]);
    }
    return substitutions;
}

FunctionSignature inherited_method_signature_without_self(const FunctionDecl& method) {
    FunctionSignature signature;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    std::vector<TypeRef> param_types;
    param_types.reserve(method.params.size() - first_param);
    for (size_t i = first_param; i < method.params.size(); ++i) {
        param_types.push_back(method.params[i].type_ref);
    }
    set_signature_param_types(signature, std::move(param_types));
    set_signature_return_type(signature, function_return_type_ref(method));
    return signature;
}

FunctionSignature inherited_method_signature_for_class_type(const ClassDecl& owner,
                                                            const TypeRef& receiver_type,
                                                            const FunctionDecl& method) {
    FunctionSignature signature = inherited_method_signature_without_self(method);
    const std::vector<TypeRef> receiver_args = template_arg_refs_from_type(receiver_type);
    const std::map<std::string, TypeRef> substitutions =
        class_type_ref_substitutions(owner, receiver_args);
    std::vector<TypeRef> param_types;
    const size_t param_count = signature_param_count(signature);
    param_types.reserve(param_count);
    for (size_t i = 0; i < param_count; ++i) {
        TypeRef param_type = signature_param_type_ref(signature, i);
        param_types.push_back(substitute_type_ref(param_type, substitutions));
    }
    set_signature_param_types(signature, std::move(param_types));
    set_signature_return_type(
        signature, substitute_type_ref(signature_return_type_ref(signature), substitutions));
    return signature;
}

bool signatures_equivalent(const FunctionSignature& a, const FunctionSignature& b) {
    if (!type_ref_equivalent(signature_return_type_ref(a), signature_return_type_ref(b)) ||
        signature_param_count(a) != signature_param_count(b)) {
        return false;
    }
    for (size_t i = 0; i < signature_param_count(a); ++i) {
        if (!type_ref_equivalent(signature_param_type_ref(a, i), signature_param_type_ref(b, i))) {
            return false;
        }
    }
    return true;
}

bool same_method_identity(const MethodIdentity& a, const MethodIdentity& b) {
    return a.name == b.name && signatures_equivalent(a.signature, b.signature);
}

FunctionSignature resolve_signature_aliases(const Symbols& symbols, FunctionSignature signature) {
    std::vector<TypeRef> params;
    const size_t param_count = signature_param_count(signature);
    params.reserve(param_count);
    for (size_t i = 0; i < param_count; ++i) {
        params.push_back(resolve_alias_ref(symbols, signature_param_type_ref(signature, i)));
    }
    set_signature_param_types(signature, std::move(params));
    set_signature_return_type(signature,
                              resolve_alias_ref(symbols, signature_return_type_ref(signature)));
    return signature;
}

bool contains_method_identity(const std::vector<MethodIdentity>& identities,
                              const MethodIdentity& candidate) {
    return std::any_of(identities.begin(), identities.end(), [&](const MethodIdentity& identity) {
        return same_method_identity(identity, candidate);
    });
}

std::string method_signature_label(std::string_view name, const FunctionSignature& signature) {
    std::ostringstream out;
    out << name << '(';
    const size_t param_count = signature_param_count(signature);
    for (size_t i = 0; i < param_count; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << substitute_type_ref_text(signature_param_type_ref(signature, i), {});
    }
    out << ") -> " << type_ref_text(signature_return_type_ref(signature));
    return out.str();
}

MethodRecord method_record_for_class_type(const Symbols& symbols, const ClassDecl& owner,
                                          const TypeRef& receiver_type,
                                          const FunctionDecl& method) {
    MethodRecord record;
    record.identity.name = method.name;
    record.identity.signature = resolve_signature_aliases(
        symbols, inherited_method_signature_for_class_type(owner, receiver_type, method));
    record.owner = owner.name;
    record.label = method_signature_label(record.identity.name, record.identity.signature);
    return record;
}

MethodIdentity method_identity_without_self(const Symbols& symbols, const FunctionDecl& method) {
    return MethodIdentity{.name = method.name,
                          .signature = resolve_signature_aliases(
                              symbols, inherited_method_signature_without_self(method))};
}

const ClassDecl* dudu_class_for_base(const Symbols& symbols, const TypeRef& base) {
    return class_for_receiver_type(symbols, base);
}

bool class_has_instance_storage(const Symbols& symbols, const ClassDecl& klass,
                                std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return false;
    }
    if (!klass.fields.empty()) {
        return true;
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base_decl.type_ref)) {
            if (class_has_instance_storage(symbols, *parent, seen)) {
                return true;
            }
        }
    }
    return false;
}

bool class_has_instance_storage(const Symbols& symbols, const ClassDecl& klass) {
    std::set<std::string> seen;
    return class_has_instance_storage(symbols, klass, seen);
}

bool class_has_abstract_method(const Symbols& symbols, const ClassDecl& klass,
                               std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return false;
    }
    for (const FunctionDecl& method : klass.methods) {
        if (has_decorator(method, "abstract")) {
            return true;
        }
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base_decl.type_ref)) {
            if (class_has_abstract_method(symbols, *parent, seen)) {
                return true;
            }
        }
    }
    return false;
}

bool class_has_abstract_method(const Symbols& symbols, const ClassDecl& klass) {
    std::set<std::string> seen;
    return class_has_abstract_method(symbols, klass, seen);
}

bool interface_like_base(const Symbols& symbols, const ClassDecl& klass) {
    return !class_has_instance_storage(symbols, klass) && class_has_abstract_method(symbols, klass);
}

void collect_inherited_fields(const Symbols& symbols, const ClassDecl& klass,
                              std::map<std::string, std::string>& fields,
                              std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return;
    }
    for (const FieldDecl& field : klass.fields) {
        fields.emplace(field.name, klass.name);
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base_decl.type_ref)) {
            collect_inherited_fields(symbols, *parent, fields, seen);
        }
    }
}

void collect_concrete_methods(const Symbols& symbols, const ClassDecl& klass,
                              const TypeRef& receiver_type, std::vector<MethodRecord>& methods,
                              std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return;
    }
    for (const FunctionDecl& method : klass.methods) {
        const bool is_static = method.params.empty() || method.params.front().name != "self";
        if (!is_static && !has_decorator(method, "abstract")) {
            methods.push_back(method_record_for_class_type(symbols, klass, receiver_type, method));
        }
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base_decl.type_ref)) {
            collect_concrete_methods(symbols, *parent, base_decl.type_ref, methods, seen);
        }
    }
}

std::vector<MethodRecord> unresolved_abstract_method_records_impl(const Symbols& symbols,
                                                                  const TypeRef& type,
                                                                  std::set<std::string>& seen) {
    const std::string unwrapped = unwrap_type(symbols, type);
    if (!seen.insert(unwrapped).second) {
        return {};
    }
    const ClassDecl* klass = class_for_receiver_type(symbols, type);
    if (klass == nullptr) {
        return {};
    }

    std::vector<MethodRecord> unresolved;
    for (const BaseClassDecl& base_decl : klass->base_class_refs) {
        std::set<std::string> branch_seen = seen;
        for (const MethodRecord& method :
             unresolved_abstract_method_records_impl(symbols, base_decl.type_ref, branch_seen)) {
            const auto existing =
                std::find_if(unresolved.begin(), unresolved.end(), [&](const MethodRecord& entry) {
                    return same_method_identity(entry.identity, method.identity);
                });
            if (existing == unresolved.end()) {
                unresolved.push_back(method);
            }
        }
    }

    for (const FunctionDecl& method : klass->methods) {
        if (method.params.empty() || method.params.front().name != "self") {
            continue;
        }
        const MethodRecord record = method_record_for_class_type(symbols, *klass, type, method);
        if (has_decorator(method, "abstract")) {
            const auto existing =
                std::find_if(unresolved.begin(), unresolved.end(), [&](const MethodRecord& entry) {
                    return same_method_identity(entry.identity, record.identity);
                });
            if (existing == unresolved.end()) {
                unresolved.push_back(record);
            }
        } else {
            unresolved.erase(std::remove_if(unresolved.begin(), unresolved.end(),
                                            [&](const MethodRecord& entry) {
                                                return same_method_identity(entry.identity,
                                                                            record.identity);
                                            }),
                             unresolved.end());
        }
    }

    return unresolved;
}

} // namespace dudu::sema_inheritance_detail
