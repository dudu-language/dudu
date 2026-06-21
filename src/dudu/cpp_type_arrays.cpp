#include "dudu/array_shape.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_type_internal.hpp"

namespace dudu {
namespace {

[[noreturn]] void malformed_fixed_array_type_ref(const TypeRef& type) {
    throw CompileError(type.location,
                       "malformed structured type node: fixed array is missing its element type");
}

} // namespace

std::string lower_fixed_array_type(const TypeRef& type) {
    if (type.children.empty()) {
        malformed_fixed_array_type_ref(type);
    }
    const TypeRef& storage = type.children.front();
    std::string out;
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        out = lower_cpp_type(storage.children.front());
    } else {
        out = lower_cpp_type(storage);
    }
    const std::vector<std::string> dims = explicit_array_shape_values(type);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
}

std::string lower_fixed_array_type(const TypeRef& type,
                                   const std::vector<std::string>& namespace_aliases) {
    if (type.children.empty()) {
        malformed_fixed_array_type_ref(type);
    }
    const TypeRef& storage = type.children.front();
    std::string out;
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        out = lower_cpp_type(storage.children.front(), namespace_aliases);
    } else {
        out = lower_cpp_type(storage, namespace_aliases);
    }
    const std::vector<std::string> dims = explicit_array_shape_values(type);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
}

std::string lower_fixed_array_type(const TypeRef& type,
                                   const std::vector<std::string>& namespace_aliases,
                                   const CppEmitOptions& options) {
    if (type.children.empty()) {
        malformed_fixed_array_type_ref(type);
    }
    const TypeRef& storage = type.children.front();
    std::string out;
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        out = lower_cpp_type(storage.children.front(), namespace_aliases, options);
    } else {
        out = lower_cpp_type(storage, namespace_aliases, options);
    }
    const std::vector<std::string> dims = explicit_array_shape_values(type);
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        out = "std::array<" + out + ", " + *it + ">";
    }
    return out;
}

} // namespace dudu
