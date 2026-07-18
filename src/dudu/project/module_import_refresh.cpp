#include "dudu/project/module_import_aliases.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace dudu {
namespace {

bool project_identity(const NativeSymbolId& identity, const std::set<std::string>& module_paths) {
    if (!identity.usr.empty() || identity.canonical_path.empty()) {
        return false;
    }
    return std::any_of(module_paths.begin(), module_paths.end(), [&](const std::string& module) {
        return identity.canonical_path.starts_with(module + ".");
    });
}

template <typename Decl, typename Predicate>
void erase_where(std::vector<Decl>& declarations, Predicate predicate) {
    declarations.erase(std::remove_if(declarations.begin(), declarations.end(), predicate),
                       declarations.end());
}

void remove_old_projections(ModuleAst& unit, const std::set<std::string>& module_paths) {
    erase_where(unit.native_types, [&](const NativeTypeDecl& declaration) {
        return project_identity(declaration.identity, module_paths);
    });
    erase_where(unit.native_values, [&](const NativeValueDecl& declaration) {
        return project_identity(declaration.identity, module_paths);
    });
    erase_where(unit.native_functions, [&](const NativeFunctionDecl& declaration) {
        return project_identity(declaration.identity, module_paths);
    });
    erase_where(unit.native_classes, [&](const ClassDecl& declaration) {
        return project_identity(declaration.identity, module_paths);
    });
    erase_where(unit.imported_enum_shapes, [&](const EnumDecl& declaration) {
        return module_paths.contains(declaration.origin_module);
    });
    unit.module_strip_prefixes.clear();
    unit.module_import_prefixes.clear();
}

const ModuleDependency* dependency_for(const ModuleAst& unit, const ImportDecl& import) {
    const auto found = std::find_if(unit.dependencies.begin(), unit.dependencies.end(),
                                    [&](const ModuleDependency& dependency) {
                                        return dependency.kind == import.kind &&
                                               dependency.import_module_path == import.module_path;
                                    });
    return found == unit.dependencies.end() ? nullptr : &*found;
}

void rebuild_unit_projections(ModuleAst& unit,
                              const std::map<std::string, ModuleAst*>& units_by_name) {
    for (const ImportDecl& import : unit.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const ModuleDependency* dependency = dependency_for(unit, import);
        if (dependency == nullptr) {
            continue;
        }
        const auto source = units_by_name.find(dependency->resolved_module_path);
        if (source == units_by_name.end()) {
            continue;
        }
        if (import.kind == ImportKind::Module) {
            add_qualified_module_symbols(unit, *source->second, import);
        } else {
            add_selective_module_symbol(unit, *source->second, import);
        }
    }
}

template <typename Decl>
void append(std::vector<Decl>& destination, const std::vector<Decl>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

void rebuild_flattened_projection(ModuleAst& module) {
    module.imports.clear();
    module.aliases.clear();
    module.native_types.clear();
    module.native_values.clear();
    module.native_functions.clear();
    module.native_macros.clear();
    module.native_namespaces.clear();
    module.native_classes.clear();
    module.imported_enum_shapes.clear();
    module.module_strip_prefixes.clear();
    module.module_import_prefixes.clear();
    module.resolved_macro_decorators.clear();
    module.generated_origins.clear();
    module.enums.clear();
    module.classes.clear();
    module.constants.clear();
    module.functions.clear();
    module.static_asserts.clear();

    for (const ModuleAst& unit : module.module_units) {
        append(module.imports, unit.imports);
        append(module.aliases, unit.aliases);
        append(module.native_types, unit.native_types);
        append(module.native_values, unit.native_values);
        append(module.native_functions, unit.native_functions);
        append(module.native_macros, unit.native_macros);
        append(module.native_namespaces, unit.native_namespaces);
        append(module.native_classes, unit.native_classes);
        append(module.imported_enum_shapes, unit.imported_enum_shapes);
        append(module.module_strip_prefixes, unit.module_strip_prefixes);
        append(module.module_import_prefixes, unit.module_import_prefixes);
        module.resolved_macro_decorators.insert(unit.resolved_macro_decorators.begin(),
                                                unit.resolved_macro_decorators.end());
        append(module.generated_origins, unit.generated_origins);
        append(module.enums, unit.enums);
        append(module.classes, unit.classes);
        append(module.constants, unit.constants);
        append(module.functions, unit.functions);
        append(module.static_asserts, unit.static_asserts);
    }
}

} // namespace

void refresh_projected_module_symbols(ModuleAst& module) {
    if (module.module_units.empty()) {
        return;
    }
    std::set<std::string> module_paths;
    std::map<std::string, ModuleAst*> units_by_name;
    for (ModuleAst& unit : module.module_units) {
        module_paths.insert(unit.module_path);
        units_by_name[unit.module_path] = &unit;
    }
    for (ModuleAst& unit : module.module_units) {
        remove_old_projections(unit, module_paths);
        rebuild_unit_projections(unit, units_by_name);
    }
    rebuild_flattened_projection(module);
}

} // namespace dudu
