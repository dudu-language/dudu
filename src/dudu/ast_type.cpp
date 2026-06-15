#include "dudu/ast_type.hpp"

#include "dudu/ast_parse_utils.hpp"
#include "dudu/cpp_lower.hpp"

namespace dudu {

std::vector<std::string> template_type_arg_texts(const TypeRef& type, std::string_view name) {
    if (type.kind != TypeKind::Template || type.name != name) {
        return {};
    }
    std::vector<std::string> out;
    out.reserve(type.children.size());
    for (const TypeRef& child : type.children) {
        out.push_back(trim_copy(child.text));
    }
    return out;
}

std::vector<std::string> template_type_arg_texts(std::string_view type, std::string_view name) {
    return template_type_arg_texts(parse_type_text(type), name);
}

std::optional<std::string> single_template_type_arg_text(const TypeRef& type,
                                                         std::string_view name) {
    const std::vector<std::string> args = template_type_arg_texts(type, name);
    if (args.size() != 1) {
        return std::nullopt;
    }
    return args.front();
}

std::optional<std::string> single_template_type_arg_text(std::string_view type,
                                                         std::string_view name) {
    return single_template_type_arg_text(parse_type_text(type), name);
}

std::optional<std::string> unary_type_child_text(const TypeRef& type, TypeKind kind) {
    if (type.kind != kind || type.children.size() != 1) {
        return std::nullopt;
    }
    return trim_copy(type.children.front().text);
}

std::optional<std::string> unary_type_child_text(std::string_view type, TypeKind kind) {
    return unary_type_child_text(parse_type_text(type), kind);
}

std::optional<std::string> unary_type_child_text(const TypeRef& type,
                                                 std::initializer_list<TypeKind> kinds) {
    if (type.children.size() != 1) {
        return std::nullopt;
    }
    for (const TypeKind kind : kinds) {
        if (type.kind == kind) {
            return trim_copy(type.children.front().text);
        }
    }
    return std::nullopt;
}

std::optional<std::string> unary_type_child_text(std::string_view type,
                                                 std::initializer_list<TypeKind> kinds) {
    return unary_type_child_text(parse_type_text(type), kinds);
}

} // namespace dudu
