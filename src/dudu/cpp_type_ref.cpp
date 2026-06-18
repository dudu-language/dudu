#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_type_internal.hpp"

#include <sstream>

namespace dudu {
namespace {

std::string lower_function_type(const TypeRef& type, bool pointer);
std::string lower_function_type(const TypeRef& type, bool pointer,
                                const std::vector<std::string>& namespace_aliases);

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

std::string join_lowered_type_args(const std::vector<TypeRef>& args,
                                   const std::vector<std::string>& namespace_aliases,
                                   const CppEmitOptions& options, size_t start = 0) {
    std::ostringstream out;
    for (size_t i = start; i < args.size(); ++i) {
        if (i > start) {
            out << ", ";
        }
        out << lower_cpp_type(args[i], namespace_aliases, options);
    }
    return out.str();
}

std::string lower_template_type(const TypeRef& type) {
    const std::string& name = type.name;
    if ((name == "std.function" || name == "std::function") && type.children.size() == 1 &&
        type.children.front().kind == TypeKind::Function) {
        return "std::function<" + lower_function_type(type.children.front(), false) + ">";
    }
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
    if (name == "variant") {
        return "std::variant<" + join_lowered_type_args(type.children) + ">";
    }
    std::ostringstream out;
    out << replace_dots(name) << "<" << join_lowered_type_args(type.children) << ">";
    return out.str();
}

std::string lower_template_type(const TypeRef& type,
                                const std::vector<std::string>& namespace_aliases) {
    const std::string& name = type.name;
    if ((name == "std.function" || name == "std::function") && type.children.size() == 1 &&
        type.children.front().kind == TypeKind::Function) {
        return "std::function<" +
               lower_function_type(type.children.front(), false, namespace_aliases) + ">";
    }
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
    if (name == "variant") {
        return "std::variant<" + join_lowered_type_args(type.children, namespace_aliases) + ">";
    }
    std::ostringstream out;
    out << replace_dots(strip_c_import_type_aliases(name, namespace_aliases)) << "<"
        << join_lowered_type_args(type.children, namespace_aliases) << ">";
    return out.str();
}

std::string lower_template_type(const TypeRef& type,
                                const std::vector<std::string>& namespace_aliases,
                                const CppEmitOptions& options) {
    const std::string& name = type.name;
    if ((name == "std.function" || name == "std::function") && type.children.size() == 1 &&
        type.children.front().kind == TypeKind::Function) {
        return "std::function<" +
               lower_cpp_type(type.children.front(), namespace_aliases, options) + ">";
    }
    if (name == "list") {
        return "std::vector<" + join_lowered_type_args(type.children, namespace_aliases, options) +
               ">";
    }
    if (name == "span") {
        return "std::span<" + join_lowered_type_args(type.children, namespace_aliases, options) +
               ">";
    }
    if (name == "strided_span") {
        return "dudu::StridedSpan<" +
               join_lowered_type_args(type.children, namespace_aliases, options) + ">";
    }
    if (name == "dict") {
        return "std::unordered_map<" +
               join_lowered_type_args(type.children, namespace_aliases, options) + ">";
    }
    if (name == "set") {
        return "std::unordered_set<" +
               join_lowered_type_args(type.children, namespace_aliases, options) + ">";
    }
    if (name == "Option") {
        return "std::optional<" +
               join_lowered_type_args(type.children, namespace_aliases, options) + ">";
    }
    if (name == "Result") {
        return "dudu::Result<" + join_lowered_type_args(type.children, namespace_aliases, options) +
               ">";
    }
    if (name == "tuple") {
        std::ostringstream out;
        out << "dudu::Tuple" << type.children.size() << "<"
            << join_lowered_type_args(type.children, namespace_aliases, options) << ">";
        return out.str();
    }
    if (name == "variant") {
        return "std::variant<" + join_lowered_type_args(type.children, namespace_aliases, options) +
               ">";
    }
    std::ostringstream out;
    out << replace_dots(
               emitted_type_name(strip_c_import_type_aliases(name, namespace_aliases), options))
        << "<" << join_lowered_type_args(type.children, namespace_aliases, options) << ">";
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

std::string lower_function_type(const TypeRef& type, bool pointer,
                                const std::vector<std::string>& namespace_aliases,
                                const CppEmitOptions& options) {
    std::ostringstream signature;
    const std::string result = type.children.empty()
                                   ? "void"
                                   : lower_cpp_type(type.children[0], namespace_aliases, options);
    signature << result << '(';
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            signature << ", ";
        }
        signature << lower_cpp_type(type.children[i], namespace_aliases, options);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

std::string lower_top_level_const_type(const TypeRef& type) {
    if (type.kind == TypeKind::Pointer && !type.children.empty()) {
        return lower_cpp_type(type.children.front()) + "* const";
    }
    if (type.kind == TypeKind::Reference) {
        return lower_cpp_type(type);
    }
    return "const " + lower_cpp_type(type);
}

std::string lower_top_level_const_type(const TypeRef& type,
                                       const std::vector<std::string>& namespace_aliases) {
    if (type.kind == TypeKind::Pointer && !type.children.empty()) {
        return lower_cpp_type(type.children.front(), namespace_aliases) + "* const";
    }
    if (type.kind == TypeKind::Reference) {
        return lower_cpp_type(type, namespace_aliases);
    }
    return "const " + lower_cpp_type(type, namespace_aliases);
}

std::string lower_top_level_const_type(const TypeRef& type,
                                       const std::vector<std::string>& namespace_aliases,
                                       const CppEmitOptions& options) {
    if (type.kind == TypeKind::Pointer && !type.children.empty()) {
        return lower_cpp_type(type.children.front(), namespace_aliases, options) + "* const";
    }
    if (type.kind == TypeKind::Reference) {
        return lower_cpp_type(type, namespace_aliases, options);
    }
    return "const " + lower_cpp_type(type, namespace_aliases, options);
}

} // namespace

std::string lower_cpp_type(const TypeRef& type) {
    if (!has_type_ref(type)) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return lower_cpp_type(type_ref_head_name(type));
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
                                     : lower_top_level_const_type(type.children[0]);
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
    if (!has_type_ref(type)) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return lower_cpp_type(type_ref_head_name(type), namespace_aliases);
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
                   : lower_top_level_const_type(type.children[0], namespace_aliases);
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

std::string lower_cpp_type(const TypeRef& type, const CppEmitOptions& options) {
    return lower_cpp_type(type, {}, options);
}

std::string lower_cpp_type(const TypeRef& type, const std::vector<std::string>& namespace_aliases,
                           const CppEmitOptions& options) {
    if (!options.use_generated_names && options.generated_type_names.empty()) {
        return lower_cpp_type(type, namespace_aliases);
    }
    if (!has_type_ref(type)) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return lower_cpp_type(type_ref_head_name(type), namespace_aliases, options);
    case TypeKind::Value:
        return type_ref_head_name(type);
    case TypeKind::Template:
        return lower_template_type(type, namespace_aliases, options);
    case TypeKind::Pointer:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases, options)
                   : lower_cpp_type(type.children[0], namespace_aliases, options) + "*";
    case TypeKind::Reference:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases, options)
                   : lower_cpp_type(type.children[0], namespace_aliases, options) + "&";
    case TypeKind::Const:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases, options)
                   : lower_top_level_const_type(type.children[0], namespace_aliases, options);
    case TypeKind::Volatile:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases, options)
                   : "volatile " + lower_cpp_type(type.children[0], namespace_aliases, options);
    case TypeKind::Atomic:
        return type.children.empty()
                   ? lower_cpp_type(type.text, namespace_aliases, options)
                   : "std::atomic<" + lower_cpp_type(type.children[0], namespace_aliases, options) +
                         ">";
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases, options)
                                     : lower_cpp_type(type.children[0], namespace_aliases, options);
    case TypeKind::Static:
        return type.children.empty() ? lower_cpp_type(type.text, namespace_aliases, options)
                                     : lower_cpp_type(type.children[0], namespace_aliases, options);
    case TypeKind::FixedArray:
        return lower_fixed_array_type(type, namespace_aliases, options);
    case TypeKind::Function:
        return lower_function_type(type, true, namespace_aliases, options);
    case TypeKind::Unknown:
        return lower_cpp_type(type.text, namespace_aliases, options);
    }
    return lower_cpp_type(type.text, namespace_aliases, options);
}

std::string lower_cpp_pointer_type(const std::string& pointee) {
    TypeRef pointer;
    pointer.kind = TypeKind::Pointer;
    pointer.text = "*" + trim_copy(pointee);
    pointer.children.push_back(parse_type_text(pointee));
    return lower_cpp_type(pointer);
}

std::string lower_cpp_pointer_type(const std::string& pointee,
                                   const std::vector<std::string>& namespace_aliases) {
    TypeRef pointer;
    pointer.kind = TypeKind::Pointer;
    pointer.text = "*" + trim_copy(pointee);
    pointer.children.push_back(parse_type_text(pointee));
    return lower_cpp_type(pointer, namespace_aliases);
}

} // namespace dudu
