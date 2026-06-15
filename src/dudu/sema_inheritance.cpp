#include "dudu/sema_inheritance.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

std::string unwrap_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        type = trim(std::move(type));
        while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
            type = trim(type.substr(1));
        }
        bool changed = false;
        for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
            const std::string prefix = std::string(wrapper) + "[";
            if (type.rfind(prefix, 0) == 0 && type.back() == ']') {
                type = trim(type.substr(prefix.size(), type.size() - prefix.size() - 1));
                changed = true;
                break;
            }
        }
        if (!changed)
            return base_type(type);
    }
}

bool ref_like(std::string type) {
    type = trim(std::move(type));
    return !type.empty() && (type.front() == '*' || type.front() == '&');
}

bool derives_from_impl(const Symbols& symbols, const std::string& derived, const std::string& base,
                       std::set<std::string>& seen) {
    if (derived == base)
        return true;
    if (!seen.insert(derived).second)
        return false;
    const auto klass = symbols.classes.find(derived);
    if (klass == symbols.classes.end())
        return false;
    for (const std::string& parent : klass->second->base_classes) {
        if (derives_from_impl(symbols, unwrap_type(symbols, parent), base, seen))
            return true;
    }
    return false;
}

bool has_decorator(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (trim(decorator.text) == name) {
            return true;
        }
    }
    return false;
}

FunctionSignature inherited_method_signature_without_self(const FunctionDecl& method) {
    FunctionSignature signature;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    for (size_t i = first_param; i < method.params.size(); ++i) {
        signature.params.push_back(method.params[i].type);
    }
    signature.return_type = method.return_type.empty() ? "void" : method.return_type;
    return signature;
}

std::string abstract_key(const FunctionDecl& method) {
    const FunctionSignature signature = inherited_method_signature_without_self(method);
    std::ostringstream out;
    out << method.name << '(';
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << signature.params[i];
    }
    out << ") -> " << signature.return_type;
    return out.str();
}

std::string method_key_without_self(const FunctionDecl& method) {
    const FunctionSignature signature = inherited_method_signature_without_self(method);
    std::ostringstream out;
    out << method.name << '(';
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << signature.params[i];
    }
    out << ") -> " << signature.return_type;
    return out.str();
}

const ClassDecl* dudu_class_for_base(const Symbols& symbols, const std::string& base) {
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
    for (const std::string& base : klass.base_classes) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base)) {
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
    for (const std::string& base : klass.base_classes) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base)) {
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
    for (const std::string& base : klass.base_classes) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base)) {
            collect_inherited_fields(symbols, *parent, fields, seen);
        }
    }
}

void collect_concrete_methods(const Symbols& symbols, const ClassDecl& klass,
                              std::map<std::string, std::string>& methods,
                              std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return;
    }
    for (const FunctionDecl& method : klass.methods) {
        const bool is_static = method.params.empty() || method.params.front().name != "self";
        if (!is_static && !has_decorator(method, "abstract")) {
            methods.emplace(method_key_without_self(method), klass.name);
        }
    }
    for (const std::string& base : klass.base_classes) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base)) {
            collect_concrete_methods(symbols, *parent, methods, seen);
        }
    }
}

std::vector<std::string> unresolved_abstract_methods_impl(const Symbols& symbols,
                                                          const std::string& type,
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
    for (const std::string& base : klass->second->base_classes) {
        std::set<std::string> branch_seen = seen;
        for (const std::string& method :
             unresolved_abstract_methods_impl(symbols, base, branch_seen)) {
            unresolved[method] = method;
        }
    }

    for (const FunctionDecl& method : klass->second->methods) {
        if (method.params.empty() || method.params.front().name != "self") {
            continue;
        }
        const std::string key = abstract_key(method);
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

bool type_derives_from(const Symbols& symbols, const std::string& derived,
                       const std::string& base) {
    std::set<std::string> seen;
    return derives_from_impl(symbols, unwrap_type(symbols, derived), unwrap_type(symbols, base),
                             seen);
}

bool native_base_assignable(const Symbols& symbols, const std::string& expected,
                            const std::string& got) {
    if (!ref_like(expected) && !ref_like(got))
        return false;
    const std::string base = unwrap_type(symbols, expected);
    const std::string derived = unwrap_type(symbols, got);
    return base != derived && type_derives_from(symbols, derived, base);
}

bool class_type_has_instance_storage(const Symbols& symbols, const std::string& type) {
    const auto klass = symbols.classes.find(unwrap_type(symbols, type));
    if (klass == symbols.classes.end()) {
        return false;
    }
    return !klass->second->fields.empty() ||
           std::any_of(klass->second->base_classes.begin(), klass->second->base_classes.end(),
                       [&](const std::string& base) {
                           return class_type_has_instance_storage(symbols, base);
                       });
}

std::vector<std::string> unimplemented_abstract_methods(const Symbols& symbols,
                                                        const std::string& type) {
    std::set<std::string> seen;
    return unresolved_abstract_methods_impl(symbols, type, seen);
}

bool is_abstract_class_type(const Symbols& symbols, const std::string& type) {
    return !unimplemented_abstract_methods(symbols, type).empty();
}

FunctionSignature method_signature_without_self(const FunctionDecl& method) {
    return inherited_method_signature_without_self(method);
}

const FunctionDecl* find_method_decl(const Symbols& symbols, const std::string& type,
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
    for (const std::string& base : klass->second->base_classes) {
        if (const FunctionDecl* method = find_method_decl(symbols, base, name)) {
            return method;
        }
    }
    return nullptr;
}

bool same_signature(const FunctionSignature& a, const FunctionSignature& b) {
    return a.return_type == b.return_type && a.params == b.params;
}

void check_multiple_inheritance_rules(const Symbols& symbols, const ClassDecl& klass) {
    if (klass.base_classes.size() <= 1) {
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

    for (const std::string& base : klass.base_classes) {
        const ClassDecl* parent = dudu_class_for_base(symbols, base);
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
        collect_concrete_methods(symbols, *parent, methods, method_seen);
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
