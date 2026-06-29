#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_type_internal.hpp"

#include <sstream>
#include <string_view>

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

} // namespace

std::string lower_template_type(const TypeRef& type) {
    const std::string_view name = type.name;
    if ((name == "std.function" || name == "std::function") && type.children.size() == 1 &&
        type.children.front().kind == TypeKind::Function) {
        return "std::function<" + lower_cpp_function_type(type.children.front(), false) + ">";
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
    if (name == "strided_span2") {
        return "dudu::StridedSpan2<" + join_lowered_type_args(type.children) + ">";
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
    out << replace_dots(std::string{name}) << "<" << join_lowered_type_args(type.children) << ">";
    return out.str();
}

std::string lower_template_type(const TypeRef& type,
                                const std::vector<std::string>& namespace_aliases) {
    const std::string_view name = type.name;
    if ((name == "std.function" || name == "std::function") && type.children.size() == 1 &&
        type.children.front().kind == TypeKind::Function) {
        return "std::function<" +
               lower_cpp_function_type(type.children.front(), false, namespace_aliases) + ">";
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
    if (name == "strided_span2") {
        return "dudu::StridedSpan2<" + join_lowered_type_args(type.children, namespace_aliases) +
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
    out << replace_dots(strip_c_import_type_aliases(std::string{name}, namespace_aliases)) << "<"
        << join_lowered_type_args(type.children, namespace_aliases) << ">";
    return out.str();
}

std::string lower_template_type(const TypeRef& type,
                                const std::vector<std::string>& namespace_aliases,
                                const CppEmitOptions& options) {
    const std::string_view name = type.name;
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
    if (name == "strided_span2") {
        return "dudu::StridedSpan2<" +
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
               emitted_type_name(strip_c_import_type_aliases(std::string{name}, namespace_aliases),
                                 options))
        << "<" << join_lowered_type_args(type.children, namespace_aliases, options) << ">";
    return out.str();
}

} // namespace dudu
