#include "dudu/lsp/language_server_semantic_index.hpp"

#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/project/project_index.hpp"

#include <optional>

namespace dudu {
namespace {

void add_native_name(std::set<std::string>& values, const std::string& name) {
    values.insert(name);
}

void add_dudu_module_symbols(DuduSemanticIndex& out, const ModuleAst& module,
                             const std::string& prefix = {}) {
    const auto qualify = [&](const std::string& name) {
        return prefix.empty() ? name : prefix + "." + name;
    };
    for (const ClassDecl& klass : module.classes) {
        out.classes.insert(qualify(klass.name));
        for (const ConstDecl& constant : klass.constants) {
            out.values.insert(qualify(klass.name + "." + constant.name));
        }
        for (const ConstDecl& field : klass.static_fields) {
            out.values.insert(qualify(klass.name + "." + field.name));
        }
        for (const FunctionDecl& method : klass.methods) {
            out.functions.insert(qualify(klass.name + "." + method.name));
        }
    }
    for (const EnumDecl& en : module.enums) {
        out.enums.insert(qualify(en.name));
        for (const EnumValueDecl& value : en.values) {
            out.enum_members.insert(qualify(en.name + "." + value.name));
        }
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        out.types.insert(qualify(alias.name));
    }
    for (const ConstDecl& constant : module.constants) {
        out.values.insert(qualify(constant.name));
    }
    for (const FunctionDecl& fn : module.functions) {
        out.functions.insert(qualify(fn.name));
    }
}

bool module_exports_name(const ModuleAst& module, const std::string& name) {
    DuduSemanticIndex symbols;
    add_dudu_module_symbols(symbols, module);
    return symbols.classes.contains(name) || symbols.enums.contains(name) ||
           symbols.enum_members.contains(name) || symbols.functions.contains(name) ||
           symbols.values.contains(name) || symbols.types.contains(name);
}

std::string root_import_name(const std::string& module_path) {
    const size_t dot = module_path.find('.');
    return dot == std::string::npos ? module_path : module_path.substr(0, dot);
}

void add_selective_import_symbol(DuduSemanticIndex& out, const ModuleAst& imported,
                                 const ImportDecl& import) {
    const std::string bound = bound_import_name(import);
    const std::string target = import.imported_name;
    if (target.empty() || bound.empty()) {
        return;
    }
    for (const ClassDecl& klass : imported.classes) {
        if (klass.name == target) {
            out.classes.insert(bound);
            return;
        }
    }
    for (const EnumDecl& en : imported.enums) {
        if (en.name == target) {
            out.enums.insert(bound);
            return;
        }
        for (const EnumValueDecl& value : en.values) {
            if (en.name + "." + value.name == target || value.name == target) {
                out.enum_members.insert(bound);
                return;
            }
        }
    }
    for (const TypeAliasDecl& alias : imported.aliases) {
        if (alias.name == target) {
            out.types.insert(bound);
            return;
        }
    }
    for (const ConstDecl& constant : imported.constants) {
        if (constant.name == target) {
            out.values.insert(bound);
            return;
        }
    }
    for (const FunctionDecl& fn : imported.functions) {
        if (fn.name == target) {
            out.functions.insert(bound);
            return;
        }
    }
}

} // namespace

NativeSemanticIndex native_semantic_index(const ModuleAst& module) {
    NativeSemanticIndex out;
    const NativeClassDefinitionIndex class_index = native_class_definition_index(module);
    for (const NativeTypeDecl& type : module.native_types) {
        add_native_name(out.types, type.name);
        if (native_alias_target_class_definition(class_index, type).has_value()) {
            add_native_name(out.class_aliases, type.name);
        }
    }
    for (const NativeValueDecl& value : module.native_values) {
        add_native_name(out.values, value.name);
    }
    for (const NativeFunctionDecl& fn : module.native_functions) {
        add_native_name(out.functions, fn.name);
    }
    for (const NativeMacroDecl& macro : module.native_macros) {
        add_native_name(out.macros, macro.name);
    }
    for (const NativeNamespaceDecl& ns : module.native_namespaces) {
        add_native_name(out.namespaces, ns.name);
    }
    for (const ClassDecl& klass : module.native_classes) {
        add_native_name(out.classes, klass.name);
        for (const ConstDecl& constant : klass.constants) {
            add_native_name(out.values, klass.name + "." + constant.name);
        }
        for (const FieldDecl& field : klass.fields) {
            add_native_name(out.values, klass.name + "." + field.name);
        }
        for (const FunctionDecl& method : klass.methods) {
            add_native_name(out.methods, klass.name + "." + method.name);
        }
    }
    for (const EnumDecl& en : module.enums) {
        for (const EnumValueDecl& value : en.values) {
            add_native_name(out.enum_members, en.name + "." + value.name);
        }
    }
    return out;
}

DuduSemanticIndex dudu_semantic_index(const ModuleAst& module) {
    DuduSemanticIndex out;
    add_dudu_module_symbols(out, module);
    return out;
}

DuduSemanticIndex dudu_semantic_index(const ProjectIndex& index, const ModuleAst& current) {
    DuduSemanticIndex out = dudu_semantic_index(current);
    for (const ImportDecl& import : current.imports) {
        if (import.kind == ImportKind::Module) {
            const std::string bound = bound_import_name(import);
            out.namespaces.insert(bound);
            if (const ModuleAst* imported = index.imported_unit(current, import)) {
                add_dudu_module_symbols(out, *imported, bound);
                if (import.alias.empty()) {
                    const std::string root = root_import_name(import.module_path);
                    if (root != bound) {
                        out.namespaces.insert(root);
                        add_dudu_module_symbols(out, *imported, root);
                    }
                }
            }
        } else if (import.kind == ImportKind::From) {
            if (const ModuleAst* imported = index.imported_unit(current, import)) {
                add_selective_import_symbol(out, *imported, import);
                if (!module_exports_name(*imported, import.imported_name)) {
                    out.values.insert(bound_import_name(import));
                }
            }
        }
    }
    return out;
}

} // namespace dudu
