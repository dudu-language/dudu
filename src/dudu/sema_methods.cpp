#include "dudu/sema_methods.hpp"

#include "dudu/sema_index.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/source.hpp"

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

std::string unwrap_receiver_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        type = trim(std::move(type));
        while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
            type = trim(type.substr(1));
        }
        bool unwrapped = false;
        for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
            const std::string prefix = std::string(wrapper) + "[";
            if (type.rfind(prefix, 0) == 0 && type.back() == ']') {
                type = trim(type.substr(prefix.size(), type.size() - prefix.size() - 1));
                unwrapped = true;
                break;
            }
        }
        if (!unwrapped) {
            return base_type(type);
        }
    }
}

bool is_indexed_local_segment(const std::string& text) {
    const size_t index = text.find('[');
    return index != std::string::npos && text.back() == ']' &&
           is_plain_identifier(trim(text.substr(0, index)));
}

bool method_is_static(const FunctionDecl& method) {
    for (const Decorator& decorator : method.decorators) {
        if (trim(decorator.text) == "staticmethod") {
            return true;
        }
    }
    return false;
}

std::string first_path_type(const Symbols& symbols,
                            const std::map<std::string, std::string>& locals,
                            const SourceLocation* location, const std::string& first,
                            const std::string& unknown_local_prefix) {
    if (is_indexed_local_segment(first)) {
        const std::string name = trim(first.substr(0, first.find('[')));
        if (location == nullptr && !locals.contains(name)) {
            return {};
        }
        return indexed_value_type(symbols, locals,
                                  location == nullptr ? SourceLocation{} : *location, name,
                                  unknown_local_prefix.empty() ? "indexed access to unknown local: "
                                                               : unknown_local_prefix);
    }
    const auto local = locals.find(first);
    if (local == locals.end()) {
        if (location != nullptr && !unknown_local_prefix.empty()) {
            fail(*location, unknown_local_prefix + first);
        }
        return {};
    }
    return local->second;
}

} // namespace

std::string member_path_type(const Symbols& symbols,
                             const std::map<std::string, std::string>& locals,
                             const SourceLocation* location, const std::string& path,
                             std::string unknown_local_prefix) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos) {
        if (const auto local = locals.find(path); local != locals.end()) {
            return local->second;
        }
        return {};
    }
    std::string current = path.substr(0, dot);
    if (symbols.classes.contains(current)) {
        size_t start = dot + 1;
        const ClassDecl* klass = symbols.classes.at(current);
        while (start < path.size()) {
            const size_t next = path.find('.', start);
            const std::string member =
                path.substr(start, next == std::string::npos ? next : next - start);
            bool found = false;
            for (const ConstDecl& constant : klass->constants) {
                if (constant.name == member) {
                    if (next == std::string::npos) {
                        return constant.type;
                    }
                    const auto next_class = symbols.classes.find(base_type(constant.type));
                    if (next_class == symbols.classes.end()) {
                        return {};
                    }
                    klass = next_class->second;
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (location != nullptr) {
                    fail(*location, "unknown static member: " + path);
                }
                return {};
            }
            start = next + 1;
        }
        return {};
    }
    std::string type = first_path_type(symbols, locals, location, current, unknown_local_prefix);
    if (type.empty())
        return {};
    size_t start = dot + 1;
    while (start < path.size()) {
        const size_t next = path.find('.', start);
        const std::string field =
            path.substr(start, next == std::string::npos ? next : next - start);
        const auto klass = symbols.classes.find(unwrap_receiver_type(symbols, type));
        if (klass == symbols.classes.end()) {
            return {};
        }
        bool found = false;
        for (const FieldDecl& decl : klass->second->fields) {
            if (decl.name == field) {
                type = decl.type;
                found = true;
                break;
            }
        }
        if (!found) {
            if (location != nullptr) {
                fail(*location, "unknown field: " + path);
            }
            return {};
        }
        if (next == std::string::npos) {
            return type;
        }
        start = next + 1;
    }
    return type;
}

bool is_member_path(const std::string& path) {
    if (path.find('.') == std::string::npos) {
        return false;
    }
    for (const std::string& part : split_top_level(path)) {
        if (part != path) {
            return false;
        }
    }
    size_t start = 0;
    while (start < path.size()) {
        const size_t dot = path.find('.', start);
        const std::string part = path.substr(start, dot == std::string::npos ? dot : dot - start);
        const std::string trimmed = trim(part);
        if (!is_plain_identifier(trimmed) && !is_indexed_local_segment(trimmed)) {
            return false;
        }
        if (dot == std::string::npos) {
            return true;
        }
        start = dot + 1;
    }
    return false;
}

bool method_signature_for_type(const Symbols& symbols, std::string receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location) {
    const std::string type = unwrap_receiver_type(symbols, std::move(receiver_type));
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name) {
            continue;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            signature.params.push_back(method.params[i].type);
        }
        signature.return_type = method.return_type.empty() ? "void" : method.return_type;
        return true;
    }
    for (const std::string& base : klass->second->base_classes) {
        if (method_signature_for_type(symbols, base, method_name, signature, nullptr)) {
            return true;
        }
    }
    if (location != nullptr) {
        fail(*location, "unknown method: " + type + "." + method_name);
    }
    return false;
}

bool static_method_signature_for_type(const Symbols& symbols, const std::string& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location) {
    const auto klass = symbols.classes.find(type_name);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name || !method_is_static(method)) {
            continue;
        }
        for (const ParamDecl& param : method.params) {
            signature.params.push_back(param.type);
        }
        signature.return_type = method.return_type.empty() ? "void" : method.return_type;
        return true;
    }
    if (location != nullptr) {
        fail(*location, "unknown static method: " + type_name + "." + method_name);
    }
    return false;
}

} // namespace dudu
