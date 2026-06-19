#include "dudu/native_header_parse.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_header_merge.hpp"
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
    if (starts_with(name, "_")) {
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
void add_unique_native_decl(std::vector<T>& out, std::map<std::string, std::string>& seen, T value,
                            std::string_view kind) {
    const std::string identity = native_decl_identity_key(value);
    const auto existing = seen.find(value.name);
    if (existing == seen.end()) {
        seen[value.name] = identity;
        out.push_back(std::move(value));
        return;
    }
    if (existing->second != identity && actionable_native_name_collision(value.name) &&
        !non_actionable_native_collision_location(value.location)) {
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
    if (existing->second != identity && actionable_native_name_collision(value.name) &&
        !non_actionable_native_collision_location(value.location)) {
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
    std::map<std::string, std::string> types;
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
