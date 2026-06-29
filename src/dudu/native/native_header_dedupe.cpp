#include "dudu/native/native_header_parse.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/native/native_header_collision.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/core/source.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <type_traits>

namespace dudu {
namespace {

template <typename T>
void add_unique_native_decl(std::vector<T>& out, std::map<std::string, T>& seen, T value,
                            std::string_view kind) {
    const std::string identity = native_decl_identity_key(value);
    const auto existing = seen.find(value.name);
    if (existing == seen.end()) {
        seen[value.name] = value;
        out.push_back(std::move(value));
        return;
    }
    bool collision = native_decl_identity_key(existing->second) != identity;
    if constexpr (std::is_same_v<T, NativeTypeDecl>) {
        collision = collision && !native_type_redeclarations_compatible(existing->second, value);
    }
    if (collision && native_decl_collision_is_error(value.name, value.location)) {
        throw CompileError(value.location,
                           "native " + std::string(kind) + " name collision: " + value.name);
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
    if (existing->second != identity && native_decl_collision_is_error(value.name, value.location)) {
        throw CompileError(value.location,
                           "native " + std::string(kind) + " name collision: " + value.name);
    }
}

bool contains_equivalent_method(const std::vector<FunctionDecl>& methods,
                                const FunctionDecl& candidate) {
    return std::ranges::any_of(methods, [&](const FunctionDecl& method) {
        if (method.name != candidate.name ||
            !type_ref_equivalent(function_return_type_ref(method),
                                 function_return_type_ref(candidate)) ||
            method.params.size() != candidate.params.size()) {
            return false;
        }
        for (size_t i = 0; i < method.params.size(); ++i) {
            if (!type_ref_equivalent(method.params[i].type_ref, candidate.params[i].type_ref)) {
                return false;
            }
        }
        return true;
    });
}

void add_unique_function(std::vector<NativeFunctionDecl>& out, NativeFunctionDecl value) {
    if (!contains_equivalent_native_function(out, value)) {
        out.push_back(std::move(value));
    }
}

bool has_equivalent_base_class(const ClassDecl& klass, const TypeRef& type) {
    return std::ranges::any_of(klass.base_class_refs, [&](const BaseClassDecl& base) {
        return type_ref_equivalent(base.type_ref, type);
    });
}

void merge_class(ClassDecl& target, const ClassDecl& source) {
    for (const BaseClassDecl& base : source.base_class_refs) {
        if (!has_equivalent_base_class(target, base.type_ref)) {
            target.base_class_refs.push_back(base);
        }
    }
    std::set<std::string> fields;
    for (const FieldDecl& field : target.fields) {
        fields.insert(field.name);
    }
    for (const FieldDecl& field : source.fields) {
        if (fields.insert(field.name).second) {
            target.fields.push_back(field);
        }
    }
    for (const FunctionDecl& method : source.methods) {
        if (!contains_equivalent_method(target.methods, method)) {
            target.methods.push_back(method);
        }
    }
}

void add_unique_class(std::vector<ClassDecl>& out, std::map<std::string, std::string>& seen,
                      ClassDecl value) {
    const std::string identity = native_decl_identity_key(value);
    const auto existing = seen.find(value.name);
    if (existing == seen.end()) {
        seen[value.name] = identity;
        out.push_back(std::move(value));
        return;
    }
    if (existing->second != identity && native_decl_collision_is_error(value.name, value.location)) {
        throw CompileError(value.location, "native class name collision: " + value.name);
    }
    for (ClassDecl& klass : out) {
        if (klass.name == value.name) {
            merge_class(klass, value);
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
    for (auto item : scan.types) {
        add_unique_native_decl(out.types, types, std::move(item), "type");
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
