#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/native/native_header_ast_parse_internal.hpp"
#include "dudu/native/native_header_identity.hpp"

#include <algorithm>
#include <regex>
#include <set>

namespace dudu::native_ast_parse {
namespace {

TypeRef normalize_native_type_ref(TypeRef type);

TypeRef structure_dependent_associated_paths(
    TypeRef type, const std::set<std::string>& dependent_type_names) {
    for (TypeRef& child : type.children) {
        child = structure_dependent_associated_paths(std::move(child), dependent_type_names);
    }
    if (type.kind != TypeKind::Qualified && type.kind != TypeKind::Template) {
        return type;
    }
    const size_t dot = type.name.find(".");
    if (dot == std::string::npos || !dependent_type_names.contains(type.name.substr(0, dot))) {
        return type;
    }

    TypeRef owner = named_type_ref(type.name.substr(0, dot), type.location);
    size_t start = dot + 1;
    size_t next = type.name.find(".", start);
    while (next != std::string::npos) {
        TypeRef associated;
        associated.kind = TypeKind::Associated;
        associated.name = type.name.substr(start, next - start);
        associated.children.push_back(std::move(owner));
        associated.location = type.location;
        owner = std::move(associated);
        start = next + 1;
        next = type.name.find(".", start);
    }

    TypeRef associated;
    associated.kind = type.kind == TypeKind::Template ? TypeKind::AssociatedTemplate
                                                      : TypeKind::Associated;
    associated.name = type.name.substr(start);
    associated.children.push_back(std::move(owner));
    if (type.kind == TypeKind::Template) {
        associated.children.insert(associated.children.end(),
                                   std::make_move_iterator(type.children.begin()),
                                   std::make_move_iterator(type.children.end()));
    }
    associated.location = type.location;
    associated.range = type.range;
    return associated;
}

bool childless_native_wrapper(const TypeRef& type) {
    return (type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
            type.kind == TypeKind::Atomic || type.kind == TypeKind::Storage ||
            type.kind == TypeKind::Shared || type.kind == TypeKind::Device ||
            type.kind == TypeKind::Static || type.kind == TypeKind::Pointer ||
            type.kind == TypeKind::Reference || type.kind == TypeKind::PackExpansion) &&
           type.children.empty();
}

bool native_dot_marker_type(const TypeRef& type) {
    return type.kind == TypeKind::Named && type.name == ".";
}

bool native_type_transform_name(std::string_view name) {
    return name == "__remove_const" || name == "__remove_volatile" ||
           name == "__remove_cv" || name == "__remove_reference" ||
           name == "__remove_cvref" || name == "__underlying_type";
}

TypeRef normalize_native_type_ref(TypeRef type) {
    if (childless_native_wrapper(type)) {
        return TypeRef{};
    }
    for (TypeRef& child : type.children) {
        child = normalize_native_type_ref(std::move(child));
    }
    if (type.kind == TypeKind::Template && type.children.size() >= 2) {
        std::string pack_name = type_ref_head_name(type.children.front());
        while (!pack_name.empty() && pack_name.back() == '.') {
            pack_name.pop_back();
        }
        bool marker_tail = !pack_name.empty();
        for (size_t i = 1; i < type.children.size(); ++i) {
            marker_tail = marker_tail && native_dot_marker_type(type.children[i]);
        }
        if (marker_tail) {
            TypeRef pack_child = named_type_ref(pack_name, type.children.front().location);
            type.children = {pack_expansion_type_ref(std::move(pack_child), type.location)};
        }
    }
    if (type.kind == TypeKind::Template && type.children.size() == 1 &&
        native_type_transform_name(type.name)) {
        type.kind = TypeKind::NativeTransform;
    }
    return type;
}

} // namespace

bool dudu_primitive_name(std::string_view name) {
    for (std::string_view primitive : {"bool", "char", "f32", "f64", "i8", "i16", "i32", "i64",
                                       "isize", "u8", "u16", "u32", "u64", "usize", "void"}) {
        if (name == primitive) {
            return true;
        }
    }
    return false;
}

NativeSymbolId scanned_identity(const NativeCursorIdentityIndex& identities, NativeCursorKind kind,
                                std::string_view spelling, const SourceLocation& location,
                                std::string canonical_path, const std::string& current_file) {
    NativeSymbolId identity = native_identity(canonical_path, current_file);
    std::optional<std::string> usr = identities.find(kind, spelling, location);
    if (!usr && (kind == NativeCursorKind::Type || kind == NativeCursorKind::Class ||
                 kind == NativeCursorKind::Namespace)) {
        usr = identities.find_semantic(kind, canonical_path);
    }
    if (usr) {
        identity.usr = *usr;
    }
    return identity;
}

std::optional<TypeLayout> scanned_layout(const NativeCursorIdentityIndex& identities,
                                         NativeCursorKind kind, std::string_view spelling,
                                         const SourceLocation& location) {
    return identities.find_layout(kind, spelling, location);
}

TypeRef parse_native_type_text(std::string text, const SourceLocation& location) {
    text = trim_string(std::move(text));
    if (text.ends_with("...")) {
        return pack_expansion_type_ref(normalize_native_type_ref(parse_type_text(
                                           trim_string(text.substr(0, text.size() - 3)), location)),
                                       location);
    }
    return normalize_native_type_ref(parse_type_text(text, location));
}

TypeRef parse_native_type_text(std::string text, const SourceLocation& location,
                               const std::vector<std::string>& dependent_type_names) {
    std::set<std::string> names;
    for (std::string name : dependent_type_names) {
        if (name.ends_with("...")) {
            name.resize(name.size() - 3);
        }
        names.insert(std::move(name));
    }
    return structure_dependent_associated_paths(
        parse_native_type_text(std::move(text), location), names);
}

void replace_native_type_placeholder(TypeRef& type, std::string_view placeholder,
                                     std::string_view replacement) {
    if ((type.kind == TypeKind::Named || type.kind == TypeKind::Qualified) &&
        type.name == placeholder) {
        type.name = std::string(replacement);
    }
    for (TypeRef& child : type.children) {
        replace_native_type_placeholder(child, placeholder, replacement);
    }
}

std::string native_type_placeholder(std::string_view index) {
    return "__dudu_native_type_parameter_" + std::string(index);
}

std::string preserve_native_type_placeholders(std::string text) {
    static const std::regex placeholder(R"(type-parameter-[0-9]+-([0-9]+))");
    std::string out;
    std::smatch match;
    while (std::regex_search(text, match, placeholder)) {
        out += match.prefix().str();
        out += native_type_placeholder(match[1].str());
        text = match.suffix().str();
    }
    out += text;
    return out;
}

} // namespace dudu::native_ast_parse
