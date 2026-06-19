#include "dudu/native_header_merge.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace dudu {
namespace {

std::string native_symbol_identity_key(const NativeSymbolId& identity) {
    if (!identity.usr.empty()) {
        return "usr:" + identity.usr;
    }
    if (!identity.canonical_path.empty()) {
        return "path:" + identity.canonical_path;
    }
    return {};
}

template <typename T> std::string native_decl_identity_key(const T& decl) {
    std::string key = native_symbol_identity_key(decl.identity);
    if (key.empty()) {
        key = "name:" + decl.name;
    }
    return key;
}

bool actionable_native_name_collision(const std::string& name) {
    if (name.rfind("_", 0) == 0) {
        return false;
    }
    static const std::set<std::string> associated_artifacts = {
        "iterator", "const_iterator", "reference", "const_reference", "value_type",
        "pointer",  "const_pointer",  "size_type", "difference_type", "type"};
    if (associated_artifacts.contains(name)) {
        return false;
    }
    return name.find('.') == std::string::npos && name.find("::") == std::string::npos;
}

bool non_actionable_native_collision_location(const SourceLocation& location) {
    const std::string& file = location.file;
    if (file.empty() || file.ends_with(".dd")) {
        return true;
    }
    return file.rfind("/usr/", 0) == 0 || file.rfind("/opt/", 0) == 0;
}

template <typename T>
void append_unique_native_decls(std::vector<T>& target, const std::vector<T>& source,
                                std::string_view kind) {
    std::map<std::string, std::string> seen_by_name;
    for (const T& item : target) {
        seen_by_name[item.name] = native_decl_identity_key(item);
    }
    for (const T& item : source) {
        const std::string identity = native_decl_identity_key(item);
        const auto existing = seen_by_name.find(item.name);
        if (existing == seen_by_name.end()) {
            seen_by_name[item.name] = identity;
            target.push_back(item);
            continue;
        }
        if (existing->second != identity && actionable_native_name_collision(item.name) &&
            !non_actionable_native_collision_location(item.location)) {
            throw CompileError(item.location,
                               "native " + std::string(kind) + " name collision: " + item.name);
        }
    }
}

} // namespace

bool type_ref_lists_equivalent(const std::vector<TypeRef>& lhs, const std::vector<TypeRef>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!type_ref_equivalent(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

bool native_function_equivalent(const NativeFunctionDecl& lhs, const NativeFunctionDecl& rhs) {
    return lhs.name == rhs.name && lhs.variadic == rhs.variadic &&
           lhs.min_params == rhs.min_params &&
           type_ref_equivalent(native_function_return_type_ref(lhs),
                               native_function_return_type_ref(rhs)) &&
           type_ref_lists_equivalent(native_function_param_type_refs(lhs),
                                     native_function_param_type_refs(rhs));
}

bool contains_equivalent_native_function(const std::vector<NativeFunctionDecl>& functions,
                                         const NativeFunctionDecl& candidate) {
    return std::ranges::any_of(functions, [&](const NativeFunctionDecl& fn) {
        return native_function_equivalent(fn, candidate);
    });
}

void append_unique_native_functions(std::vector<NativeFunctionDecl>& target,
                                    const std::vector<NativeFunctionDecl>& source) {
    for (const NativeFunctionDecl& item : source) {
        if (!contains_equivalent_native_function(target, item)) {
            target.push_back(item);
        }
    }
}

void append_unique_native_types(std::vector<NativeTypeDecl>& target,
                                const std::vector<NativeTypeDecl>& source) {
    append_unique_native_decls(target, source, "type");
}

void append_unique_native_values(std::vector<NativeValueDecl>& target,
                                 const std::vector<NativeValueDecl>& source) {
    append_unique_native_decls(target, source, "value");
}

void append_unique_native_macros(std::vector<NativeMacroDecl>& target,
                                 const std::vector<NativeMacroDecl>& source) {
    append_unique_native_decls(target, source, "macro");
}

void append_unique_native_namespaces(std::vector<NativeNamespaceDecl>& target,
                                     const std::vector<NativeNamespaceDecl>& source) {
    append_unique_native_decls(target, source, "namespace");
}

void append_unique_native_classes(std::vector<ClassDecl>& target,
                                  const std::vector<ClassDecl>& source) {
    append_unique_native_decls(target, source, "class");
}

} // namespace dudu
