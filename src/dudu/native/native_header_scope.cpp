#include "dudu/native/native_header_scope.hpp"

#include "dudu/codegen/cpp_lower.hpp"

#include <algorithm>
#include <optional>

namespace dudu {
namespace {

bool scan_has_type(const NativeHeaderScan& scan, const std::string& name) {
    for (const NativeTypeDecl& type : scan.types)
        if (type.name == name)
            return true;
    for (const ClassDecl& klass : scan.classes)
        if (klass.name == name)
            return true;
    return false;
}

std::optional<std::string>
class_scoped_type_name(const NativeHeaderScan& scan,
                       const std::vector<std::pair<int, size_t>>* classes,
                       const std::string& name) {
    if (classes == nullptr) {
        return std::nullopt;
    }
    for (auto it = classes->rbegin(); it != classes->rend(); ++it) {
        const ClassDecl& klass = scan.classes[it->second];
        if (std::ranges::find(klass.generic_params, name) != klass.generic_params.end()) {
            return name;
        }
        if (std::ranges::any_of(klass.type_aliases,
                                [&](const TypeAliasDecl& alias) { return alias.name == name; })) {
            return klass.name + "." + name;
        }
        const std::string nested = klass.name + "." + name;
        if (scan_has_type(scan, nested)) {
            return nested;
        }
    }
    return std::nullopt;
}

std::string qualify_scoped_type_impl(const NativeHeaderScan& scan,
                                     const std::vector<std::pair<int, std::string>>& namespaces,
                                     const std::vector<std::pair<int, size_t>>* classes,
                                     std::string type) {
    type = trim_copy(std::move(type));
    if (type.empty())
        return type;
    if (type.front() == '*' || type.front() == '&')
        return std::string(1, type.front()) +
               qualify_scoped_type_impl(scan, namespaces, classes, type.substr(1));
    for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
        const std::string prefix = std::string(wrapper) + "[";
        if (starts_with(type, prefix) && ends_with(type, "]"))
            return prefix +
                   qualify_scoped_type_impl(
                       scan, namespaces, classes,
                       type.substr(prefix.size(), type.size() - prefix.size() - 1)) +
                   "]";
    }
    const size_t open = type.find('[');
    if (open != std::string::npos && ends_with(type, "]")) {
        std::string out =
            qualify_scoped_type_impl(scan, namespaces, classes, type.substr(0, open)) + "[";
        bool first = true;
        for (std::string arg :
             split_top_level_args(type.substr(open + 1, type.size() - open - 2))) {
            if (!first) {
                out += ", ";
            }
            first = false;
            out += qualify_scoped_type_impl(scan, namespaces, classes, std::move(arg));
        }
        out.push_back(']');
        return out;
    }
    if (type.find('.') != std::string::npos)
        return type;
    if (const auto scoped = class_scoped_type_name(scan, classes, type)) {
        return *scoped;
    }
    const std::string scoped = join_scope(namespaces, type);
    return scan_has_type(scan, scoped) ? scoped : type;
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
    if (!classes.empty())
        return scan.classes[classes.back().second].name + "." + name;
    return join_scope(namespaces, name);
}

std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                std::string type) {
    return qualify_scoped_type_impl(scan, namespaces, nullptr, std::move(type));
}

std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                const std::vector<std::pair<int, size_t>>& classes,
                                std::string type) {
    return qualify_scoped_type_impl(scan, namespaces, &classes, std::move(type));
}

std::vector<std::string> qualify_scoped_types(
    const NativeHeaderScan& scan, const std::vector<std::pair<int, std::string>>& namespaces,
    const std::vector<std::pair<int, size_t>>& classes, std::vector<std::string> types) {
    for (std::string& type : types)
        type = qualify_scoped_type(scan, namespaces, classes, std::move(type));
    return types;
}

std::vector<std::string>
qualify_scoped_types(const NativeHeaderScan& scan,
                     const std::vector<std::pair<int, std::string>>& namespaces,
                     std::vector<std::string> types) {
    for (std::string& type : types)
        type = qualify_scoped_type(scan, namespaces, std::move(type));
    return types;
}

} // namespace dudu
