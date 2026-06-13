#include "dudu/sema_methods.hpp"

#include "dudu/source.hpp"

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
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
    const auto local = locals.find(current);
    if (local == locals.end()) {
        if (location != nullptr) {
            fail(*location, unknown_local_prefix + current);
        }
        return {};
    }
    std::string type = local->second;
    size_t start = dot + 1;
    while (start < path.size()) {
        const size_t next = path.find('.', start);
        const std::string field =
            path.substr(start, next == std::string::npos ? next : next - start);
        const auto klass = symbols.classes.find(base_type(type));
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

bool method_signature_for_type(const Symbols& symbols, std::string receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location) {
    const std::string type = base_type(resolve_alias(symbols, std::move(receiver_type)));
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
    if (location != nullptr) {
        fail(*location, "unknown method: " + type + "." + method_name);
    }
    return false;
}

} // namespace dudu
