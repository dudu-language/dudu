#include "dudu/native_header_scope.hpp"

#include "dudu/cpp_lower.hpp"

namespace dudu {
namespace {

bool scan_has_type(const NativeHeaderScan& scan, const std::string& name) {
    for (const NativeTypeDecl& type : scan.types)
        if (type.name == name) return true;
    for (const ClassDecl& klass : scan.classes)
        if (klass.name == name) return true;
    return false;
}

} // namespace

std::string join_scope(const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::string& name) {
    std::string out;
    for (const auto& [depth, ns] : namespaces) {
        (void)depth;
        out += ns + ".";
    }
    return out + name;
}

std::string class_name(const NativeHeaderScan& scan,
                       const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::vector<std::pair<int, size_t>>& classes,
                       const std::string& name) {
    if (!classes.empty()) return scan.classes[classes.back().second].name + "." + name;
    return join_scope(namespaces, name);
}

std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                std::string type) {
    type = trim_copy(std::move(type));
    if (type.empty()) return type;
    if (type.front() == '*' || type.front() == '&')
        return std::string(1, type.front()) +
               qualify_scoped_type(scan, namespaces, type.substr(1));
    for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
        const std::string prefix = std::string(wrapper) + "[";
        if (starts_with(type, prefix) && ends_with(type, "]"))
            return prefix + qualify_scoped_type(scan, namespaces,
                                                type.substr(prefix.size(),
                                                            type.size() - prefix.size() - 1)) +
                   "]";
    }
    const size_t open = type.find('[');
    if (open != std::string::npos && ends_with(type, "]")) {
        std::string out =
            qualify_scoped_type(scan, namespaces, type.substr(0, open)) + "[";
        bool first = true;
        for (std::string arg :
             split_top_level_args(type.substr(open + 1, type.size() - open - 2))) {
            if (!first) {
                out += ", ";
            }
            first = false;
            out += qualify_scoped_type(scan, namespaces, std::move(arg));
        }
        out.push_back(']');
        return out;
    }
    if (type.find('.') != std::string::npos) return type;
    const std::string scoped = join_scope(namespaces, type);
    return scan_has_type(scan, scoped) ? scoped : type;
}

std::vector<std::string> qualify_scoped_types(
    const NativeHeaderScan& scan, const std::vector<std::pair<int, std::string>>& namespaces,
    std::vector<std::string> types) {
    for (std::string& type : types) type = qualify_scoped_type(scan, namespaces, std::move(type));
    return types;
}

} // namespace dudu
