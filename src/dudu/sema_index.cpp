#include "dudu/sema_index.hpp"

#include "dudu/cpp_lower.hpp"

namespace dudu {

std::string indexed_value_type(const Symbols& symbols,
                               const std::map<std::string, std::string>& locals,
                               const SourceLocation& location, const std::string& name,
                               std::string_view unknown_message) {
    const auto local = locals.find(name);
    if (local == locals.end()) {
        throw CompileError(location, std::string(unknown_message) + name);
    }
    std::string type = resolve_alias(symbols, local->second);
    bool pointer_index = false;
    if (starts_with(type, "*")) {
        type = trim(type.substr(1));
        pointer_index = true;
    }
    for (const char* wrapper : {"storage", "shared", "device", "volatile", "atomic"}) {
        const std::string prefix = std::string(wrapper) + "[";
        if (starts_with(type, prefix) && type.back() == ']') {
            type = trim(type.substr(prefix.size(), type.size() - prefix.size() - 1));
            break;
        }
    }
    if (pointer_index) {
        return type;
    }
    if (starts_with(type, "list[") && type.back() == ']') {
        return trim(type.substr(5, type.size() - 6));
    }
    if (starts_with(type, "dict[") && type.back() == ']') {
        const std::vector<std::string> args = split_top_level(type.substr(5, type.size() - 6));
        if (args.size() == 2) {
            return args[1];
        }
    }
    const size_t type_index = type.find('[');
    if (type_index != std::string::npos && type.back() == ']') {
        return trim(type.substr(0, type_index));
    }
    throw CompileError(location, "cannot index non-container: " + name);
}

} // namespace dudu
