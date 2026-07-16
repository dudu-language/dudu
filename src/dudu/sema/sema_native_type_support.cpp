#include "dudu/sema/sema_native_type_support.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_header_identity.hpp"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

using NativeTypesByName = std::map<std::string, std::vector<const NativeTypeDecl*>>;
using NativeClassesByName = std::map<std::string, std::vector<const ClassDecl*>>;

void collect_type_names(const TypeRef& type, std::set<std::string>& names) {
    const std::string name = type_ref_head_name(type);
    if (!name.empty()) {
        names.insert(name);
    }
    for (const TypeRef& child : type.children) {
        collect_type_names(child, names);
    }
}

void collect_function_types(const FunctionDecl& fn, std::set<std::string>& names) {
    collect_type_names(fn.receiver_type_ref, names);
    for (const ParamDecl& param : fn.params) {
        collect_type_names(param.type_ref, names);
    }
    collect_type_names(fn.return_type_ref, names);
}

void collect_native_function_types(const NativeFunctionDecl& fn, std::set<std::string>& names) {
    for (const TypeRef& param : fn.param_type_refs) {
        collect_type_names(param, names);
    }
    collect_type_names(fn.return_type_ref, names);
}

void collect_class_types(const ClassDecl& klass, std::set<std::string>& names) {
    for (const TypeRef& type : klass.generic_default_args) {
        collect_type_names(type, names);
    }
    for (const TypeRef& type : klass.native_specialization_args) {
        collect_type_names(type, names);
    }
    for (const BaseClassDecl& base : klass.base_class_refs) {
        collect_type_names(base.type_ref, names);
    }
    for (const TypeAliasDecl& alias : klass.type_aliases) {
        collect_type_names(alias.type_ref, names);
    }
    for (const FieldDecl& field : klass.fields) {
        collect_type_names(field.type_ref, names);
    }
    for (const ConstDecl& constant : klass.constants) {
        collect_type_names(constant.type_ref, names);
    }
    for (const ConstDecl& field : klass.static_fields) {
        collect_type_names(field.type_ref, names);
    }
    for (const FunctionDecl& method : klass.methods) {
        collect_function_types(method, names);
    }
}

bool module_projection(const NativeSymbolId& identity) {
    return identity.usr.empty() && !identity.canonical_path.empty();
}

std::set<std::string> module_type_names(const ModuleAst& module) {
    std::set<std::string> names;
    for (const TypeAliasDecl& alias : module.aliases) {
        collect_type_names(alias.type_ref, names);
    }
    for (const NativeTypeDecl& type : module.native_types) {
        if (module_projection(type.identity)) {
            collect_type_names(type.type_ref, names);
        }
    }
    for (const NativeValueDecl& value : module.native_values) {
        if (module_projection(value.identity)) {
            collect_type_names(value.type_ref, names);
        }
    }
    for (const NativeFunctionDecl& fn : module.native_functions) {
        if (module_projection(fn.identity)) {
            collect_native_function_types(fn, names);
        }
    }
    for (const EnumDecl& en : module.enums) {
        collect_type_names(en.underlying_type_ref, names);
        for (const EnumValueDecl& value : en.values) {
            for (const EnumPayloadField& field : value.payload_fields) {
                collect_type_names(field.type_ref, names);
            }
        }
        for (const FunctionDecl& method : en.methods) {
            collect_function_types(method, names);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        collect_class_types(klass, names);
    }
    for (const ClassDecl& klass : module.native_classes) {
        if (!klass.native_declaration) {
            collect_class_types(klass, names);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        collect_type_names(constant.type_ref, names);
    }
    for (const FunctionDecl& fn : module.functions) {
        collect_function_types(fn, names);
    }
    return names;
}

std::vector<const ModuleAst*> reachable_dependencies(const ModuleAst& module,
                                                     const ModuleAst& tree) {
    std::map<std::string, const ModuleAst*> units;
    for (const ModuleAst& unit : tree.module_units) {
        units[unit.module_path] = &unit;
    }

    std::vector<const ModuleAst*> out;
    std::set<std::string> visited;
    std::deque<std::string> pending;
    for (const ModuleDependency& dependency : module.dependencies) {
        pending.push_back(dependency.resolved_module_path);
    }
    while (!pending.empty()) {
        std::string path = std::move(pending.front());
        pending.pop_front();
        if (!visited.insert(path).second) {
            continue;
        }
        const auto found = units.find(path);
        if (found == units.end()) {
            continue;
        }
        out.push_back(found->second);
        for (const ModuleDependency& dependency : found->second->dependencies) {
            pending.push_back(dependency.resolved_module_path);
        }
    }
    return out;
}

bool scanned_native_type(const NativeTypeDecl& type) {
    return (!type.identity.usr.empty() || !type.identity.canonical_path.empty()) &&
           (has_type_ref(type.type_ref) || !type.native_spelling.empty());
}

void index_native_types(const std::vector<const ModuleAst*>& dependencies, NativeTypesByName& types,
                        NativeClassesByName& classes) {
    for (const ModuleAst* dependency : dependencies) {
        for (const NativeTypeDecl& type : dependency->native_types) {
            if (scanned_native_type(type)) {
                types[type.name].push_back(&type);
            }
        }
        for (const ClassDecl& klass : dependency->native_classes) {
            if (!klass.native_declaration) {
                continue;
            }
            classes[klass.name].push_back(&klass);
            const std::string untagged = native_type_name_without_tag(klass.name);
            if (untagged != klass.name) {
                classes[untagged].push_back(&klass);
            }
        }
    }
}

std::string require_identity(const NativeSymbolId& identity, std::string_view name,
                             const SourceLocation& location) {
    const std::string key = native_symbol_identity_key(identity);
    if (key.empty()) {
        throw CompileError(location,
                           "native type is missing canonical identity: " + std::string(name));
    }
    return key;
}

void bind_native_identity(Symbols& symbols, const std::string& name, const std::string& identity,
                          const SourceLocation& location) {
    const auto existing = symbols.native_type_identity_by_binding.find(name);
    if (existing != symbols.native_type_identity_by_binding.end() && existing->second != identity) {
        throw CompileError(location,
                           "native type binding '" + name + "' resolves to multiple declarations");
    }
    symbols.native_type_identity_by_binding[name] = identity;
}

void add_path_prefix(Symbols& symbols, const std::string& name) {
    const size_t dot = name.find('.');
    if (dot != std::string::npos && dot > 0) {
        symbols.native_path_prefixes.insert(name.substr(0, dot));
    }
}

void add_native_type(Symbols& symbols, const NativeTypeDecl& type) {
    const std::string identity = require_identity(type.identity, type.name, type.location);
    bind_native_identity(symbols, type.name, identity, type.location);
    symbols.types.insert(type.name);
    symbols.native_types.insert(type.name);
    symbols.native_type_decls_by_identity[identity][type.name] = &type;
    add_path_prefix(symbols, type.name);
    if ((has_type_ref(type.type_ref) || !type.native_spelling.empty()) &&
        !symbols.alias_type_refs.contains(type.name)) {
        symbols.alias_type_refs[type.name] = native_type_alias_type_ref(type);
        symbols.alias_generic_params[type.name] = type.generic_params;
        symbols.alias_generic_defaults[type.name] = type.generic_default_args;
    }
}

void add_native_class_binding(Symbols& symbols, const ClassDecl& klass, const std::string& name,
                              const std::string& identity) {
    bind_native_identity(symbols, name, identity, klass.location);
    const auto existing = symbols.classes.find(name);
    if (existing != symbols.classes.end() && existing->second != &klass &&
        !existing->second->native_declaration) {
        throw CompileError(klass.location,
                           "native type binding '" + name + "' collides with a Dudu class");
    }
    symbols.types.insert(name);
    symbols.native_class_decls_by_identity[identity][name] = &klass;
    symbols.classes[name] = &klass;
    add_path_prefix(symbols, name);
}

void add_native_class(Symbols& symbols, const ClassDecl& klass) {
    if (!klass.native_specialization_args.empty()) {
        symbols.native_class_specializations[klass.name].push_back(klass);
        return;
    }
    const std::string identity = require_identity(klass.identity, klass.name, klass.location);
    add_native_class_binding(symbols, klass, klass.name, identity);
    const std::string untagged = native_type_name_without_tag(klass.name);
    if (untagged != klass.name) {
        add_native_class_binding(symbols, klass, untagged, identity);
    }
}

const ClassDecl* primary_class(const std::vector<const ClassDecl*>& classes) {
    const ClassDecl* selected = nullptr;
    for (const ClassDecl* candidate : classes) {
        if (!candidate->native_specialization_args.empty()) {
            continue;
        }
        if (selected == nullptr) {
            selected = candidate;
            continue;
        }
        if (selected->native_partial_specialization && !candidate->native_partial_specialization) {
            selected = candidate;
            continue;
        }
        if (!selected->native_partial_specialization && !candidate->native_partial_specialization &&
            native_symbol_identity_key(selected->identity) !=
                native_symbol_identity_key(candidate->identity)) {
            throw CompileError(candidate->location, "native type binding '" + candidate->name +
                                                        "' resolves to multiple declarations");
        }
    }
    return selected;
}

} // namespace

void add_imported_native_type_support(Symbols& symbols, const ModuleAst& module,
                                      const ModuleAst& module_tree) {
    const std::vector<const ModuleAst*> dependencies = reachable_dependencies(module, module_tree);
    if (dependencies.empty()) {
        return;
    }

    NativeTypesByName native_types;
    NativeClassesByName native_classes;
    index_native_types(dependencies, native_types, native_classes);

    std::set<std::string> pending_names = module_type_names(module);
    std::set<std::string> visited;
    while (!pending_names.empty()) {
        const std::string name = *pending_names.begin();
        pending_names.erase(pending_names.begin());
        if (!visited.insert(name).second) {
            continue;
        }

        bool has_primary_class = false;
        if (const auto found = native_classes.find(name); found != native_classes.end()) {
            if (const ClassDecl* klass = primary_class(found->second)) {
                add_native_class(symbols, *klass);
                has_primary_class = true;
            }
            for (const ClassDecl* specialization : found->second) {
                if (!specialization->native_specialization_args.empty()) {
                    add_native_class(symbols, *specialization);
                }
            }
        }
        if (!has_primary_class) {
            if (const auto found = native_types.find(name); found != native_types.end()) {
                for (const NativeTypeDecl* type : found->second) {
                    add_native_type(symbols, *type);
                    collect_type_names(type->type_ref, pending_names);
                    for (const TypeRef& default_arg : type->generic_default_args) {
                        collect_type_names(default_arg, pending_names);
                    }
                }
            }
        }
    }
}

} // namespace dudu
