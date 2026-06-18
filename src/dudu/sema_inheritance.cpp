#include "dudu/sema_inheritance.hpp"

#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/decorators.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_method_templates.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

std::string unwrap_type(const Symbols& symbols, const TypeRef& type);

std::string unwrap_type(const Symbols& symbols, const TypeRef& type) {
    TypeRef current = type;
    while (true) {
        const std::string rendered = type_ref_text(current);
        TypeRef resolved = resolve_alias_ref_with_legacy_fallback(symbols, current);
        if (type_ref_text(resolved) != rendered) {
            current = resolved;
            continue;
        }
        if (const auto inner =
                unary_type_child_ref(current, {TypeKind::Pointer, TypeKind::Reference})) {
            current = *inner;
            continue;
        }
        if (const auto inner = unary_type_child_ref(
                current, {TypeKind::Const, TypeKind::Volatile, TypeKind::Atomic, TypeKind::Storage,
                          TypeKind::Shared, TypeKind::Device})) {
            current = *inner;
            continue;
        }
        const std::string head = type_ref_head_name(current);
        return head.empty() ? trim(type_ref_text(current)) : head;
    }
}

bool ref_like(const TypeRef& type) {
    return type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference;
}

bool derives_from_impl(const Symbols& symbols, const TypeRef& derived_type, const std::string& base,
                       std::set<std::string>& seen) {
    const std::string derived = unwrap_type(symbols, derived_type);
    if (derived == base)
        return true;
    if (!seen.insert(derived).second)
        return false;
    const auto klass = symbols.classes.find(derived);
    if (klass == symbols.classes.end())
        return false;
    for (const BaseClassDecl& parent : klass->second->base_class_refs) {
        if (derives_from_impl(symbols, parent.type_ref, base, seen))
            return true;
    }
    return false;
}

bool has_decorator(const FunctionDecl& fn, std::string_view name) {
    return dudu::has_decorator(fn.decorators, name);
}

std::map<std::string, std::string> class_type_substitutions(const ClassDecl& owner,
                                                            const std::vector<std::string>& args) {
    std::map<std::string, std::string> substitutions;
    for (size_t i = 0; i < owner.generic_params.size() && i < args.size(); ++i) {
        substitutions.emplace(owner.generic_params[i], trim(args[i]));
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
    const std::vector<std::string> receiver_args = template_args_from_type(receiver_type);
    const std::map<std::string, std::string> substitutions =
        class_type_substitutions(owner, receiver_args);
    std::vector<TypeRef> param_types;
    param_types.reserve(signature.params.size());
    for (size_t i = 0; i < signature.params.size(); ++i) {
        TypeRef param_type = i < signature.param_type_refs.size()
                                 ? signature.param_type_refs[i]
                                 : parse_type_text(signature.params[i], method.location);
        param_types.push_back(substitute_type_ref(param_type, substitutions));
    }
    set_signature_param_types(signature, std::move(param_types));
    set_signature_return_type(signature,
                              substitute_type_ref(signature_return_type_ref(signature),
                                                  substitutions));
    return signature;
}

std::string signature_key(std::string_view name, const FunctionSignature& signature) {
    std::ostringstream out;
    out << name << '(';
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << substitute_type_ref_text(signature_param_type_ref(signature, i), {});
    }
    out << ") -> " << signature_return_type_text(signature);
    return out.str();
}

std::string abstract_key(const ClassDecl& owner, const TypeRef& receiver_type,
                         const FunctionDecl& method) {
    return signature_key(method.name,
                         inherited_method_signature_for_class_type(owner, receiver_type, method));
}

std::string method_key_without_self(const ClassDecl& owner, const TypeRef& receiver_type,
                                    const FunctionDecl& method) {
    return signature_key(method.name,
                         inherited_method_signature_for_class_type(owner, receiver_type, method));
}

std::string method_key_without_self(const FunctionDecl& method) {
    return signature_key(method.name, inherited_method_signature_without_self(method));
}

const ClassDecl* dudu_class_for_base(const Symbols& symbols, const TypeRef& base) {
    const auto found = symbols.classes.find(unwrap_type(symbols, base));
    return found == symbols.classes.end() ? nullptr : found->second;
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
                              const TypeRef& receiver_type,
                              std::map<std::string, std::string>& methods,
                              std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return;
    }
    for (const FunctionDecl& method : klass.methods) {
        const bool is_static = method.params.empty() || method.params.front().name != "self";
        if (!is_static && !has_decorator(method, "abstract")) {
            methods.emplace(method_key_without_self(klass, receiver_type, method), klass.name);
        }
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base_decl.type_ref)) {
            collect_concrete_methods(symbols, *parent, base_decl.type_ref, methods, seen);
        }
    }
}

std::vector<std::string> unresolved_abstract_methods_impl(const Symbols& symbols,
                                                          const TypeRef& type,
                                                          std::set<std::string>& seen) {
    const std::string unwrapped = unwrap_type(symbols, type);
    if (!seen.insert(unwrapped).second) {
        return {};
    }
    const auto klass = symbols.classes.find(unwrapped);
    if (klass == symbols.classes.end()) {
        return {};
    }

    std::map<std::string, std::string> unresolved;
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        std::set<std::string> branch_seen = seen;
        for (const std::string& method :
             unresolved_abstract_methods_impl(symbols, base_decl.type_ref, branch_seen)) {
            unresolved[method] = method;
        }
    }

    for (const FunctionDecl& method : klass->second->methods) {
        if (method.params.empty() || method.params.front().name != "self") {
            continue;
        }
        const std::string key = abstract_key(*klass->second, type, method);
        if (has_decorator(method, "abstract")) {
            unresolved[key] = key;
        } else {
            unresolved.erase(key);
        }
    }

    std::vector<std::string> out;
    for (const auto& [_, method] : unresolved) {
        out.push_back(method);
    }
    return out;
}

} // namespace

bool native_base_assignable(const Symbols& symbols, const TypeRef& expected, const TypeRef& got) {
    if (!ref_like(expected) && !ref_like(got))
        return false;
    const std::string base = unwrap_type(symbols, expected);
    const std::string derived = unwrap_type(symbols, got);
    std::set<std::string> seen;
    return base != derived && derives_from_impl(symbols, got, base, seen);
}

bool class_type_has_instance_storage(const Symbols& symbols, const TypeRef& type) {
    const auto klass = symbols.classes.find(unwrap_type(symbols, type));
    if (klass == symbols.classes.end()) {
        return false;
    }
    return !klass->second->fields.empty() ||
           std::any_of(klass->second->base_class_refs.begin(), klass->second->base_class_refs.end(),
                       [&](const BaseClassDecl& base) {
                           return class_type_has_instance_storage(symbols, base.type_ref);
                       });
}

std::vector<std::string> unimplemented_abstract_methods(const Symbols& symbols,
                                                        const TypeRef& type) {
    std::set<std::string> seen;
    return unresolved_abstract_methods_impl(symbols, type, seen);
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
    const auto klass = symbols.classes.find(unwrap_type(symbols, type));
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name == name) {
            return InheritedMethod{
                .method = &method,
                .signature =
                    inherited_method_signature_for_class_type(*klass->second, type, method),
            };
        }
    }
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        if (auto method = find_inherited_method(symbols, base_decl.type_ref, name)) {
            return method;
        }
    }
    return std::nullopt;
}

const FunctionDecl* find_method_decl(const Symbols& symbols, const TypeRef& type,
                                     const std::string& name) {
    const auto klass = symbols.classes.find(unwrap_type(symbols, type));
    if (klass == symbols.classes.end()) {
        return nullptr;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name == name) {
            return &method;
        }
    }
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        if (const FunctionDecl* method = find_method_decl(symbols, base_decl.type_ref, name)) {
            return method;
        }
    }
    return nullptr;
}

bool same_signature(const FunctionSignature& a, const FunctionSignature& b) {
    if (signature_return_type_text(a) != signature_return_type_text(b) ||
        a.params.size() != b.params.size()) {
        return false;
    }
    for (size_t i = 0; i < a.params.size(); ++i) {
        if (substitute_type_ref_text(signature_param_type_ref(a, i), {}) !=
            substitute_type_ref_text(signature_param_type_ref(b, i), {})) {
            return false;
        }
    }
    return true;
}

void check_multiple_inheritance_rules(const Symbols& symbols, const ClassDecl& klass) {
    if (klass.base_class_refs.size() <= 1) {
        return;
    }
    size_t storage_bases = 0;
    std::map<std::string, std::string> inherited_fields;
    std::map<std::string, std::string> inherited_concrete_methods;
    std::set<std::string> derived_overrides;
    for (const FunctionDecl& method : klass.methods) {
        if (has_decorator(method, "override")) {
            derived_overrides.insert(method_key_without_self(method));
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

        std::map<std::string, std::string> methods;
        std::set<std::string> method_seen;
        collect_concrete_methods(symbols, *parent, base_decl.type_ref, methods, method_seen);
        for (const auto& [key, owner] : methods) {
            if (const auto existing = inherited_concrete_methods.find(key);
                existing != inherited_concrete_methods.end() && existing->second != owner &&
                !derived_overrides.contains(key)) {
                throw CompileError(klass.location, "ambiguous inherited concrete method: " + key);
            }
            inherited_concrete_methods.emplace(key, owner);
        }
    }
}

} // namespace dudu
