#include "dudu/ast_type.hpp"

#include "dudu/ast_parse_utils.hpp"
#include "dudu/cpp_lower.hpp"

#include <sstream>

namespace dudu {
namespace {

std::string join_substituted_types(const std::vector<TypeRef>& types, size_t start,
                                   const std::map<std::string, std::string>& substitutions) {
    std::ostringstream out;
    for (size_t i = start; i < types.size(); ++i) {
        if (i > start) {
            out << ", ";
        }
        out << substitute_type_ref_text(types[i], substitutions);
    }
    return out.str();
}

std::string substitute_wrapper(std::string_view name, const TypeRef& type,
                               const std::map<std::string, std::string>& substitutions) {
    if (type.children.empty()) {
        return trim_copy(type.text);
    }
    return std::string(name) + "[" + substitute_type_ref_text(type.children[0], substitutions) +
           "]";
}

} // namespace

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

std::vector<TypeRef> template_type_arg_refs(const TypeRef& type, std::string_view name) {
    if (type.kind != TypeKind::Template || type.name != name) {
        return {};
    }
    return type.children;
}

std::optional<std::string> first_template_type_arg_text(const TypeRef& type) {
    if (type.kind != TypeKind::Template || type.children.empty()) {
        return std::nullopt;
    }
    return trim_copy(type.children.front().text);
}

std::optional<std::string> first_template_type_arg_text(std::string_view type) {
    return first_template_type_arg_text(parse_type_text(type));
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

std::string type_ref_head_name(const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Template:
        return trim_copy(type.name.empty() ? type.text : type.name);
    case TypeKind::Value:
        return trim_copy(type.value.empty() ? type.text : type.value);
    case TypeKind::Function:
        return "fn";
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Const:
    case TypeKind::Volatile:
    case TypeKind::Atomic:
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
    case TypeKind::Static:
    case TypeKind::FixedArray:
    case TypeKind::Unknown:
        return trim_copy(type.text);
    }
    return trim_copy(type.text);
}

std::string substitute_type_ref_text(const TypeRef& type,
                                     const std::map<std::string, std::string>& substitutions) {
    const std::string name = trim_copy(type.name.empty() ? type.text : type.name);
    if (const auto found = substitutions.find(name); found != substitutions.end()) {
        return found->second;
    }

    switch (type.kind) {
    case TypeKind::Pointer:
        return type.children.empty()
                   ? trim_copy(type.text)
                   : "*" + substitute_type_ref_text(type.children[0], substitutions);
    case TypeKind::Reference:
        return type.children.empty()
                   ? trim_copy(type.text)
                   : "&" + substitute_type_ref_text(type.children[0], substitutions);
    case TypeKind::Const:
        return substitute_wrapper("const", type, substitutions);
    case TypeKind::Volatile:
        return substitute_wrapper("volatile", type, substitutions);
    case TypeKind::Atomic:
        return substitute_wrapper("atomic", type, substitutions);
    case TypeKind::Device:
        return substitute_wrapper("device", type, substitutions);
    case TypeKind::Storage:
        return substitute_wrapper("storage", type, substitutions);
    case TypeKind::Shared:
        return substitute_wrapper("shared", type, substitutions);
    case TypeKind::Static:
        return substitute_wrapper("static", type, substitutions);
    case TypeKind::Template:
        return trim_copy(type.name) + "[" +
               join_substituted_types(type.children, 0, substitutions) + "]";
    case TypeKind::FixedArray:
        return type.children.empty() ? trim_copy(type.text)
                                     : substitute_type_ref_text(type.children[0], substitutions) +
                                           "[" + trim_copy(type.value) + "]";
    case TypeKind::Function: {
        const std::string result = type.children.empty()
                                       ? "void"
                                       : substitute_type_ref_text(type.children[0], substitutions);
        return "fn(" + join_substituted_types(type.children, 1, substitutions) + ") -> " + result;
    }
    case TypeKind::Value:
        return trim_copy(type.value.empty() ? type.text : type.value);
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Unknown:
        return trim_copy(type.text);
    }
    return trim_copy(type.text);
}

} // namespace dudu
