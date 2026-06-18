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

std::vector<TypeRef> template_type_arg_refs(const TypeRef& type, std::string_view name) {
    if (type.kind != TypeKind::Template || type.name != name) {
        return {};
    }
    return type.children;
}

std::vector<TypeRef>
template_type_arg_refs_resolved(const TypeRef& type, std::string_view name,
                                const std::map<std::string, std::string>& aliases) {
    std::vector<TypeRef> refs = template_type_arg_refs(type, name);
    if (!refs.empty()) {
        return refs;
    }
    const std::string rendered = substitute_type_ref_text(type, {});
    const auto found = aliases.find(rendered);
    if (found == aliases.end()) {
        return {};
    }
    return template_type_arg_refs(parse_type_text(found->second), name);
}

std::optional<TypeRef> unary_type_child_ref(const TypeRef& type, TypeKind kind) {
    if (type.kind != kind || type.children.size() != 1) {
        return std::nullopt;
    }
    return type.children.front();
}

std::optional<TypeRef> unary_type_child_ref(const TypeRef& type,
                                            std::initializer_list<TypeKind> kinds) {
    if (type.children.size() != 1) {
        return std::nullopt;
    }
    for (const TypeKind kind : kinds) {
        if (type.kind == kind) {
            return type.children.front();
        }
    }
    return std::nullopt;
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

bool type_ref_contains_kind(const TypeRef& type, TypeKind kind) {
    if (type.kind == kind) {
        return true;
    }
    for (const TypeRef& child : type.children) {
        if (type_ref_contains_kind(child, kind)) {
            return true;
        }
    }
    return false;
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
        if (type.name.find("::") != std::string::npos) {
            return trim_copy(type.name) + "<" +
                   join_substituted_types(type.children, 0, substitutions) + ">";
        }
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

std::string type_ref_text(const TypeRef& type) {
    return substitute_type_ref_text(type, {});
}

bool has_type_ref(const TypeRef& type) {
    return type.kind != TypeKind::Unknown || !trim_copy(type.text).empty() ||
           !trim_copy(type.name).empty() || !trim_copy(type.value).empty() ||
           !type.children.empty();
}

bool type_ref_is_name(const TypeRef& type, std::string_view name) {
    return type.kind == TypeKind::Named && type_ref_head_name(type) == name;
}

bool type_ref_is_auto(const TypeRef& type) {
    return type_ref_is_name(type, "auto");
}

bool type_ref_is_void(const TypeRef& type) {
    return type_ref_is_name(type, "void");
}

TypeRef void_type_ref(SourceLocation location) {
    return parse_type_text("void", location);
}

bool function_has_receiver_type(const FunctionDecl& fn) {
    return has_type_ref(fn.receiver_type_ref);
}

std::string function_receiver_type_text(const FunctionDecl& fn) {
    return function_has_receiver_type(fn) ? type_ref_text(fn.receiver_type_ref) : std::string{};
}

bool function_has_return_type(const FunctionDecl& fn) {
    return has_type_ref(fn.return_type_ref);
}

TypeRef function_return_type_ref(const FunctionDecl& fn) {
    return function_has_return_type(fn) ? fn.return_type_ref : void_type_ref(fn.location);
}

std::string function_return_type_text(const FunctionDecl& fn) {
    return type_ref_text(function_return_type_ref(fn));
}

TypeRef substitute_type_ref(const TypeRef& type,
                            const std::map<std::string, std::string>& substitutions) {
    const std::string name = trim_copy(type.name.empty() ? type.text : type.name);
    if (const auto found = substitutions.find(name); found != substitutions.end()) {
        return parse_type_text(found->second, type.location);
    }

    TypeRef out = type;
    for (TypeRef& child : out.children) {
        child = substitute_type_ref(child, substitutions);
    }
    out.text = substitute_type_ref_text(type, substitutions);
    return out;
}

} // namespace dudu
