#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_type_internal.hpp"

#include <optional>

namespace dudu {
namespace {

std::optional<TypeRef> reparse_fixed_array_type(const TypeRef& type) {
    if (!type.children.empty() || type.text.empty()) {
        return std::nullopt;
    }
    TypeRef parsed = parse_type_text(type.text, type.location);
    if (parsed.kind != TypeKind::FixedArray || parsed.children.empty()) {
        return std::nullopt;
    }
    return parsed;
}

} // namespace

std::string lower_fixed_array_type(const TypeRef& type) {
    if (type.children.empty()) {
        if (const auto parsed = reparse_fixed_array_type(type)) {
            return lower_fixed_array_type(*parsed);
        }
        return lower_cpp_type(type.text);
    }
    const TypeRef& storage = type.children.front();
    std::string out;
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        out = lower_cpp_type(storage.children.front());
    } else {
        out = lower_cpp_type(storage);
    }
    const std::vector<std::string> dims = split_top_level_args(type.value);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
}

std::string lower_fixed_array_type(const TypeRef& type,
                                   const std::vector<std::string>& namespace_aliases) {
    if (type.children.empty()) {
        if (const auto parsed = reparse_fixed_array_type(type)) {
            return lower_fixed_array_type(*parsed, namespace_aliases);
        }
        return lower_cpp_type(type.text, namespace_aliases);
    }
    const TypeRef& storage = type.children.front();
    std::string out;
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        out = lower_cpp_type(storage.children.front(), namespace_aliases);
    } else {
        out = lower_cpp_type(storage, namespace_aliases);
    }
    const std::vector<std::string> dims = split_top_level_args(type.value);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
}

std::string lower_fixed_array_type(const TypeRef& type,
                                   const std::vector<std::string>& namespace_aliases,
                                   const CppEmitOptions& options) {
    if (type.children.empty()) {
        if (const auto parsed = reparse_fixed_array_type(type)) {
            return lower_fixed_array_type(*parsed, namespace_aliases, options);
        }
        return lower_cpp_type(type.text, namespace_aliases, options);
    }
    const TypeRef& storage = type.children.front();
    std::string out;
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        out = lower_cpp_type(storage.children.front(), namespace_aliases, options);
    } else {
        out = lower_cpp_type(storage, namespace_aliases, options);
    }
    const std::vector<std::string> dims = split_top_level_args(type.value);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
}

} // namespace dudu
