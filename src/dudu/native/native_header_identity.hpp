#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_type.hpp"

#include <string>
#include <utility>

namespace dudu {

inline NativeSymbolId native_identity(std::string canonical_path) {
    NativeSymbolId id;
    id.canonical_path = std::move(canonical_path);
    return id;
}

inline NativeSymbolId native_identity(std::string canonical_path, const std::string& source_file) {
    NativeSymbolId id = native_identity(std::move(canonical_path));
    if (!source_file.empty()) {
        id.usr = source_file + "::" + id.canonical_path;
    }
    return id;
}

inline std::string native_symbol_identity_key(const NativeSymbolId& identity) {
    if (!identity.usr.empty()) {
        return "usr:" + identity.usr;
    }
    if (!identity.canonical_path.empty()) {
        return "path:" + identity.canonical_path;
    }
    return {};
}

inline NativeSymbolId native_class_member_identity(const ClassDecl& klass,
                                                   const std::string& member) {
    NativeSymbolId identity = klass.identity;
    if (!identity.usr.empty()) {
        identity.usr += "." + member;
    }
    if (!identity.canonical_path.empty()) {
        identity.canonical_path += "." + member;
    } else if (!klass.name.empty()) {
        identity.canonical_path = klass.name + "." + member;
    }
    return identity;
}

inline std::string native_class_member_symbol_identity_key(const ClassDecl& klass,
                                                           const std::string& member) {
    return native_symbol_identity_key(native_class_member_identity(klass, member));
}

inline std::string native_class_binding_key(const ClassDecl& klass) {
    std::string key = klass.name;
    if (!klass.native_specialization_args.empty()) {
        key += "[";
        for (size_t i = 0; i < klass.native_specialization_args.size(); ++i) {
            if (i > 0) {
                key += ",";
            }
            key += type_ref_text(klass.native_specialization_args[i]);
        }
        key += "]";
    }
    return key;
}

template <typename T> std::string native_decl_identity_key(const T& decl) {
    std::string key = native_symbol_identity_key(decl.identity);
    if (key.empty()) {
        key = "name:" + decl.name;
    }
    return key;
}

inline bool native_type_redeclarations_compatible(const NativeTypeDecl& lhs,
                                                  const NativeTypeDecl& rhs) {
    return lhs.native_spelling.empty() && rhs.native_spelling.empty() &&
           lhs.type_ref.kind == TypeKind::Unknown && rhs.type_ref.kind == TypeKind::Unknown;
}

} // namespace dudu
