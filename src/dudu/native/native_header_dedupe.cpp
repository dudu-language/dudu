#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/native/native_header_collision.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/native/native_header_parse.hpp"

#include <algorithm>
#include <map>
#include <type_traits>

namespace dudu {
namespace {

template <typename T>
void add_unique_native_decl(std::vector<T>& out, std::map<std::string, T>& seen, T value,
                            std::string_view kind,
                            const std::map<std::string, std::string>& aliases = {}) {
    const std::string identity = native_decl_identity_key(value);
    const auto existing = seen.find(value.name);
    if (existing == seen.end()) {
        seen[value.name] = value;
        out.push_back(std::move(value));
        return;
    }
    bool collision = native_decl_identity_key(existing->second) != identity;
    if constexpr (std::is_same_v<T, NativeTypeDecl>) {
        collision =
            collision && !native_type_redeclarations_compatible(existing->second, value, aliases);
    } else if constexpr (std::is_same_v<T, NativeNamespaceDecl>) {
        collision = false;
    }
    if (collision && native_decl_collision_is_error(value.name, value.location)) {
        std::string message = "native " + std::string(kind) + " name collision: " + value.name;
        if constexpr (std::is_same_v<T, NativeTypeDecl>) {
            message += " (existing " + native_decl_identity_key(existing->second) + " as '" +
                       existing->second.native_spelling + "', incoming " + identity + " as '" +
                       value.native_spelling + "')";
        }
        throw CompileError(value.location, std::move(message));
    }
    if constexpr (std::is_same_v<T, NativeTypeDecl>) {
        if (!collision) {
            merge_native_type_declaration(existing->second, value);
            for (NativeTypeDecl& declaration : out) {
                if (declaration.name == value.name) {
                    merge_native_type_declaration(declaration, value);
                    break;
                }
            }
        }
    }
}

template <typename T>
void add_unique_native_decl(std::vector<T>& out, std::map<std::string, std::string>& seen, T value,
                            std::string_view kind) {
    const std::string identity = native_decl_identity_key(value);
    const auto existing = seen.find(value.name);
    if (existing == seen.end()) {
        seen[value.name] = identity;
        out.push_back(std::move(value));
        return;
    }
    bool collision = existing->second != identity;
    if constexpr (std::is_same_v<T, NativeNamespaceDecl>) {
        collision = false;
    }
    if (collision && native_decl_collision_is_error(value.name, value.location)) {
        throw CompileError(value.location,
                           "native " + std::string(kind) + " name collision: " + value.name);
    }
}

void add_unique_function(std::vector<NativeFunctionDecl>& out, NativeFunctionDecl value) {
    if (!has_type_ref(value.return_type_ref) ||
        std::ranges::any_of(value.param_type_refs,
                            [](const TypeRef& type) { return !has_type_ref(type); })) {
        return;
    }
    if (!contains_equivalent_native_function(out, value)) {
        out.push_back(std::move(value));
    }
}

void add_unique_class(std::vector<ClassDecl>& out, std::map<std::string, std::string>& seen,
                      ClassDecl value) {
    const std::string identity = native_decl_identity_key(value);
    const std::string binding = native_class_binding_key(value);
    const auto existing = seen.find(binding);
    if (existing == seen.end()) {
        seen[binding] = identity;
        out.push_back(std::move(value));
        return;
    }
    if (existing->second != identity &&
        native_decl_collision_is_error(value.name, value.location)) {
        throw CompileError(value.location, "native class name collision: " + value.name);
    }
    for (ClassDecl& klass : out) {
        if (native_class_binding_key(klass) == binding) {
            merge_native_class_declaration(klass, value);
        }
    }
}

} // namespace

NativeHeaderScan dedupe_scan(NativeHeaderScan scan) {
    NativeHeaderScan out;
    std::map<std::string, NativeTypeDecl> types;
    std::map<std::string, std::string> values;
    std::map<std::string, std::string> macros;
    std::map<std::string, std::string> namespaces;
    std::map<std::string, std::string> classes;
    std::map<std::string, std::string> aliases;
    for (const NativeTypeDecl& item : scan.types) {
        if (!item.native_spelling.empty()) {
            aliases.insert_or_assign(item.name, item.native_spelling);
        }
    }
    for (auto item : scan.types) {
        add_unique_native_decl(out.types, types, std::move(item), "type", aliases);
    }
    for (auto item : scan.values) {
        add_unique_native_decl(out.values, values, std::move(item), "value");
    }
    for (auto item : scan.functions) {
        add_unique_function(out.functions, std::move(item));
    }
    for (auto item : scan.macros) {
        add_unique_native_decl(out.macros, macros, std::move(item), "macro");
    }
    for (auto item : scan.namespaces) {
        add_unique_native_decl(out.namespaces, namespaces, std::move(item), "namespace");
    }
    for (auto item : scan.classes) {
        add_unique_class(out.classes, classes, std::move(item));
    }
    return out;
}

} // namespace dudu
