#include "dudu/sema_inheritance.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <set>
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
        if (!changed) return base_type(type);
    }
}

bool ref_like(std::string type) {
    type = trim(std::move(type));
    return !type.empty() && (type.front() == '*' || type.front() == '&');
}

bool derives_from_impl(const Symbols& symbols, const std::string& derived,
                       const std::string& base, std::set<std::string>& seen) {
    if (derived == base) return true;
    if (!seen.insert(derived).second) return false;
    const auto klass = symbols.classes.find(derived);
    if (klass == symbols.classes.end()) return false;
    for (const std::string& parent : klass->second->base_classes) {
        if (derives_from_impl(symbols, unwrap_type(symbols, parent), base, seen)) return true;
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

FunctionSignature method_signature_without_self(const FunctionDecl& method) {
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
    const FunctionSignature signature = method_signature_without_self(method);
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
        for (const std::string& method : unresolved_abstract_methods_impl(symbols, base,
                                                                          branch_seen)) {
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
    if (!ref_like(expected) && !ref_like(got)) return false;
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

} // namespace dudu
