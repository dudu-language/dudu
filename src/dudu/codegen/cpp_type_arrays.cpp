#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_type_internal.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"

#include <algorithm>
#include <utility>

namespace dudu {
namespace {

[[noreturn]] void malformed_fixed_array_type_ref(const TypeRef& type) {
    throw CompileError(type.location,
                       "malformed structured type node: fixed array is missing its element type");
}

std::string lower_array_dimensions(std::string element, const TypeRef& type) {
    const std::vector<std::string> dims = explicit_array_shape_values(type);
    const bool has_pack =
        std::ranges::any_of(dims, [](const std::string& dim) { return dim.ends_with("..."); });
    if (has_pack) {
        std::string out = "dudu::NestedArray<" + element;
        for (const std::string& dim : dims) {
            out += ", " + dim;
        }
        return out + ">";
    }
    for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
        element = "std::array<" + element + ", " + *it + ">";
    }
    return element;
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
    return lower_array_dimensions(std::move(out), type);
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
    return lower_array_dimensions(std::move(out), type);
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
    return lower_array_dimensions(std::move(out), type);
}

} // namespace dudu
