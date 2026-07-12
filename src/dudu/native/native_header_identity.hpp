#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_type.hpp"

#include <map>
#include <set>
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
    const auto matching_tag = [](const NativeTypeDecl& tagged, const NativeTypeDecl& alias) {
        if (!alias.native_spelling.empty() || alias.type_ref.kind != TypeKind::Unknown) {
            return false;
        }
        for (std::string_view prefix : {"struct ", "union "}) {
            if (tagged.native_spelling.starts_with(prefix) &&
                tagged.native_spelling.substr(prefix.size()) == tagged.name &&
                (tagged.type_ref.kind == TypeKind::Unknown ||
                 (tagged.type_ref.kind == TypeKind::Named &&
                  tagged.type_ref.name == tagged.name))) {
                return true;
            }
        }
        return false;
    };
    if (matching_tag(lhs, rhs) || matching_tag(rhs, lhs)) {
        return true;
    }
    if (!lhs.native_spelling.empty() && lhs.native_spelling == rhs.native_spelling) {
        return true;
    }
    if (has_type_ref(lhs.type_ref) && has_type_ref(rhs.type_ref) &&
        type_ref_equivalent(lhs.type_ref, rhs.type_ref)) {
        return true;
    }
    return lhs.native_spelling.empty() && rhs.native_spelling.empty() &&
           lhs.type_ref.kind == TypeKind::Unknown && rhs.type_ref.kind == TypeKind::Unknown;
}

inline std::string resolve_native_alias_target(std::string target,
                                               const std::map<std::string, std::string>& aliases) {
    std::set<std::string> visited;
    while (!target.empty() && visited.insert(target).second) {
        const auto alias = aliases.find(target);
        if (alias == aliases.end() || alias->second.empty()) {
            break;
        }
        target = alias->second;
    }
    return target;
}

inline bool
native_type_redeclarations_compatible(const NativeTypeDecl& lhs, const NativeTypeDecl& rhs,
                                      const std::map<std::string, std::string>& aliases) {
    if (native_type_redeclarations_compatible(lhs, rhs)) {
        return true;
    }
    const std::string left = resolve_native_alias_target(lhs.native_spelling, aliases);
    const std::string right = resolve_native_alias_target(rhs.native_spelling, aliases);
    return !left.empty() && left == right;
}

} // namespace dudu
