#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_type_internal.hpp"

#include <sstream>

namespace dudu {
namespace {

std::string join_lowered_type_args(const std::vector<TypeRef>& args, size_t start = 0) {
    std::ostringstream out;
    for (size_t i = start; i < args.size(); ++i) {
        if (i > start) {
            out << ", ";
        }
        out << lower_cpp_type(args[i]);
    }
    return out.str();
}

std::string join_lowered_type_args(const std::vector<TypeRef>& args,
                                   const std::vector<std::string>& namespace_aliases,
                                   size_t start = 0) {
    std::ostringstream out;
    for (size_t i = start; i < args.size(); ++i) {
        if (i > start) {
            out << ", ";
        }
        out << lower_cpp_type(args[i], namespace_aliases);
    }
    return out.str();
}

std::string lower_template_type(const TypeRef& type) {
    const std::string& name = type.name;
    if (name == "list") {
        return "std::vector<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "span") {
        return "std::span<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "strided_span") {
        return "dudu::StridedSpan<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "dict") {
        return "std::unordered_map<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "set") {
        return "std::unordered_set<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "Option") {
        return "std::optional<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "Result") {
        return "dudu::Result<" + join_lowered_type_args(type.children) + ">";
    }
    if (name == "tuple") {
        std::ostringstream out;
        out << "dudu::Tuple" << type.children.size() << "<" << join_lowered_type_args(type.children)
            << ">";
        return out.str();
    }
    std::ostringstream out;
    out << replace_dots(name) << "<" << join_lowered_type_args(type.children) << ">";
    return out.str();
}

std::string lower_template_type(const TypeRef& type,
                                const std::vector<std::string>& namespace_aliases) {
    const std::string& name = type.name;
    if (name == "list") {
        return "std::vector<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    if (name == "span") {
        return "std::span<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    if (name == "strided_span") {
        return "dudu::StridedSpan<" + join_lowered_type_args(type.children, namespace_aliases) +
               ">";
    }
    if (name == "dict") {
        return "std::unordered_map<" + join_lowered_type_args(type.children, namespace_aliases) +
               ">";
    }
    if (name == "set") {
        return "std::unordered_set<" + join_lowered_type_args(type.children, namespace_aliases) +
               ">";
    }
    if (name == "Option") {
        return "std::optional<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    if (name == "Result") {
        return "dudu::Result<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    if (name == "tuple") {
        std::ostringstream out;
        out << "dudu::Tuple" << type.children.size() << "<"
            << join_lowered_type_args(type.children, namespace_aliases) << ">";
        return out.str();
    }
    std::ostringstream out;
    out << replace_dots(strip_c_import_type_aliases(name, namespace_aliases)) << "<"
        << join_lowered_type_args(type.children, namespace_aliases) << ">";
    return out.str();
}

std::string lower_function_type(const TypeRef& type, bool pointer) {
    std::ostringstream signature;
    const std::string result = type.children.empty() ? "void" : lower_cpp_type(type.children[0]);
    signature << result << '(';
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            signature << ", ";
        }
        signature << lower_cpp_type(type.children[i]);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

std::string lower_function_type(const TypeRef& type, bool pointer,
                                const std::vector<std::string>& namespace_aliases) {
    std::ostringstream signature;
    const std::string result =
        type.children.empty() ? "void" : lower_cpp_type(type.children[0], namespace_aliases);
    signature << result << '(';
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            signature << ", ";
        }
        signature << lower_cpp_type(type.children[i], namespace_aliases);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

std::string lower_fixed_array_type(const TypeRef& type) {
    if (type.children.empty()) {
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

} // namespace

std::string lower_cpp_type(const TypeRef& type) {
    if (type.text.empty()) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return lower_cpp_type(type.text);
    case TypeKind::Value:
        return type_ref_head_name(type);
    case TypeKind::Template:
        return lower_template_type(type);
    case TypeKind::Pointer:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : lower_cpp_type(type.children[0]) + "*";
    case TypeKind::Reference:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : lower_cpp_type(type.children[0]) + "&";
    case TypeKind::Const:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : "const " + lower_cpp_type(type.children[0]);
    case TypeKind::Volatile:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : "volatile " + lower_cpp_type(type.children[0]);
    case TypeKind::Atomic:
        return type.children.empty() ? lower_cpp_type(type.text)
                                     : "std::atomic<" + lower_cpp_type(type.children[0]) + ">";
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
        return type.children.empty() ? lower_cpp_type(type.text) : lower_cpp_type(type.children[0]);
    case TypeKind::Static:
        return type.children.empty() ? lower_cpp_type(type.text) : lower_cpp_type(type.children[0]);
    case TypeKind::FixedArray:
        return lower_fixed_array_type(type);
    case TypeKind::Function:
        return lower_function_type(type, true);
    case TypeKind::Unknown:
        return lower_cpp_type(type.text);
    }
    return lower_cpp_type(type.text);
}

std::string lower_cpp_type(const TypeRef& type, const std::vector<std::string>& namespace_aliases) {
    if (namespace_aliases.empty()) {
        return lower_cpp_type(type);
    }
    if (type.text.empty()) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return lower_cpp_type(type.text, namespace_aliases);
    case TypeKind::Value:
        return type_ref_head_name(type);
    case TypeKind::Template:
        return lower_template_type(type, namespace_aliases);
    case TypeKind::Pointer:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : lower_cpp_type(type.children[0], namespace_aliases) + "*";
    case TypeKind::Reference:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : lower_cpp_type(type.children[0], namespace_aliases) + "&";
    case TypeKind::Const:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases)
                   : "const " + lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::Volatile:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases)
                   : "volatile " + lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::Atomic:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases)
                   : "std::atomic<" + lower_cpp_type(type.children[0], namespace_aliases) + ">";
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::Static:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases)
                                     : lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::FixedArray:
        return lower_fixed_array_type(type, namespace_aliases);
    case TypeKind::Function:
        return lower_function_type(type, true, namespace_aliases);
    case TypeKind::Unknown:
        return lower_cpp_type(type.text, namespace_aliases);
    }
    return lower_cpp_type(type.text, namespace_aliases);
}

std::string lower_cpp_pointer_type(const std::string& pointee) {
    return lower_cpp_type(parse_type_text("*" + trim_copy(pointee)));
}

std::string lower_cpp_pointer_type(const std::string& pointee,
                                   const std::vector<std::string>& namespace_aliases) {
    return lower_cpp_type(parse_type_text("*" + trim_copy(pointee)), namespace_aliases);
}

} // namespace dudu
