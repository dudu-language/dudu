#include "dudu/module_import_aliases.hpp"

#include "dudu/ast_type.hpp"

#include <map>

namespace dudu {
namespace {

void add_module_type_alias(ModuleAst& module, const std::string& prefix, const std::string& name,
                           const std::string& type, const SourceLocation& location) {
    module.native_types.push_back(
        {.name = prefix + "." + name, .type = type, .location = location});
}

std::map<std::string, std::string> qualified_type_substitutions(const ModuleAst& dependency,
                                                                const std::string& prefix) {
    std::map<std::string, std::string> out;
    for (const TypeAliasDecl& alias : dependency.aliases) {
        out[alias.name] = prefix + "." + alias.name;
    }
    for (const EnumDecl& en : dependency.enums) {
        out[en.name] = prefix + "." + en.name;
    }
    for (const ClassDecl& klass : dependency.classes) {
        out[klass.name] = prefix + "." + klass.name;
    }
    return out;
}

std::map<std::string, std::string> selective_type_substitutions(const ModuleAst& dependency,
                                                                const ImportDecl& import) {
    const std::string exposed_name = import.alias.empty() ? import.imported_name : import.alias;
    std::map<std::string, std::string> out;
    for (const TypeAliasDecl& alias : dependency.aliases) {
        out[alias.name] = alias.name == import.imported_name
                              ? exposed_name
                              : import.module_path + "." + alias.name;
    }
    for (const EnumDecl& en : dependency.enums) {
        out[en.name] =
            en.name == import.imported_name ? exposed_name : import.module_path + "." + en.name;
    }
    for (const ClassDecl& klass : dependency.classes) {
        out[klass.name] = klass.name == import.imported_name
                              ? exposed_name
                              : import.module_path + "." + klass.name;
    }
    for (const NativeTypeDecl& type : dependency.native_types) {
        if (type.name.find('.') == std::string::npos && !type.type.empty()) {
            out[type.name] = type.type;
        }
    }
    return out;
}

void add_function_alias(ModuleAst& module, const FunctionDecl& fn, const std::string& name,
                        const std::map<std::string, std::string>& type_substitutions,
                        const SourceLocation& location) {
    NativeFunctionDecl alias;
    alias.name = name;
    alias.template_params = fn.generic_params;
    alias.return_type = function_has_return_type(fn)
                            ? substitute_type_ref_text(fn.return_type_ref, type_substitutions)
                            : "void";
    alias.return_type_ref = function_has_return_type(fn)
                                ? substitute_type_ref(fn.return_type_ref, type_substitutions)
                                : parse_type_text("void", location);
    alias.location = location;
    for (const ParamDecl& param : fn.params) {
        TypeRef param_type = substitute_type_ref(param.type_ref, type_substitutions);
        alias.params.push_back(substitute_type_ref_text(param_type, {}));
        alias.param_type_refs.push_back(std::move(param_type));
    }
    module.native_functions.push_back(std::move(alias));
}

FunctionDecl substituted_method(FunctionDecl method,
                                const std::map<std::string, std::string>& type_substitutions) {
    if (function_has_return_type(method)) {
        method.return_type_ref = substitute_type_ref(method.return_type_ref, type_substitutions);
    }
    for (ParamDecl& param : method.params) {
        param.type_ref = substitute_type_ref(param.type_ref, type_substitutions);
    }
    return method;
}

ClassDecl imported_class_shape(ClassDecl klass, const std::string& name,
                               const std::map<std::string, std::string>& type_substitutions,
                               const SourceLocation& location) {
    klass.name = name;
    klass.location = location;
    for (FieldDecl& field : klass.fields) {
        field.type_ref = substitute_type_ref(field.type_ref, type_substitutions);
    }
    for (ConstDecl& constant : klass.constants) {
        constant.type_ref = substitute_type_ref(constant.type_ref, type_substitutions);
    }
    for (ConstDecl& field : klass.static_fields) {
        field.type_ref = substitute_type_ref(field.type_ref, type_substitutions);
    }
    for (FunctionDecl& method : klass.methods) {
        method = substituted_method(std::move(method), type_substitutions);
    }
    return klass;
}

} // namespace

void add_qualified_module_symbols(ModuleAst& module, const ModuleAst& dependency,
                                  const ImportDecl& import) {
    const std::string prefix = import.alias.empty() ? import.module_path : import.alias;
    if (prefix.empty()) {
        return;
    }
    const std::map<std::string, std::string> type_substitutions =
        qualified_type_substitutions(dependency, prefix);
    module.module_strip_prefixes.push_back(prefix);
    for (const TypeAliasDecl& alias : dependency.aliases) {
        add_module_type_alias(module, prefix, alias.name,
                              substitute_type_ref_text(alias.type_ref, type_substitutions),
                              import.location);
    }
    for (const EnumDecl& en : dependency.enums) {
        add_module_type_alias(module, prefix, en.name, prefix + "." + en.name, import.location);
    }
    for (const ClassDecl& klass : dependency.classes) {
        add_module_type_alias(module, prefix, klass.name, prefix + "." + klass.name,
                              import.location);
        module.native_classes.push_back(imported_class_shape(klass, prefix + "." + klass.name,
                                                             type_substitutions, import.location));
    }
    for (const ConstDecl& constant : dependency.constants) {
        const TypeRef value_type = substitute_type_ref(constant.type_ref, type_substitutions);
        module.native_values.push_back({.name = prefix + "." + constant.name,
                                        .type = substitute_type_ref_text(value_type, {}),
                                        .type_ref = value_type,
                                        .location = import.location});
    }
    for (const FunctionDecl& fn : dependency.functions) {
        add_function_alias(module, fn, prefix + "." + fn.name, type_substitutions, import.location);
    }
}

void add_selective_module_symbol(ModuleAst& module, const ModuleAst& dependency,
                                 const ImportDecl& import) {
    const std::string exposed_name = import.alias.empty() ? import.imported_name : import.alias;
    const std::map<std::string, std::string> type_substitutions =
        selective_type_substitutions(dependency, import);
    module.module_strip_prefixes.push_back(import.module_path);
    for (const EnumDecl& en : dependency.enums) {
        if (en.name != import.imported_name) {
            module.native_types.push_back({.name = import.module_path + "." + en.name,
                                           .type = import.module_path + "." + en.name,
                                           .location = import.location});
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        if (klass.name != import.imported_name) {
            module.native_types.push_back({.name = import.module_path + "." + klass.name,
                                           .type = import.module_path + "." + klass.name,
                                           .location = import.location});
            module.native_classes.push_back(imported_class_shape(
                klass, import.module_path + "." + klass.name, type_substitutions, import.location));
        }
    }
    for (const TypeAliasDecl& alias : dependency.aliases) {
        if (alias.name == import.imported_name) {
            module.native_types.push_back(
                {.name = exposed_name,
                 .type = substitute_type_ref_text(alias.type_ref, type_substitutions),
                 .location = import.location});
            return;
        }
    }
    for (const EnumDecl& en : dependency.enums) {
        if (en.name == import.imported_name) {
            module.native_types.push_back({.name = exposed_name,
                                           .type = import.module_path + "." + en.name,
                                           .location = import.location});
            module.native_types.push_back({.name = import.module_path + "." + en.name,
                                           .type = import.module_path + "." + en.name,
                                           .location = import.location});
            return;
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        if (klass.name == import.imported_name) {
            module.native_types.push_back({.name = exposed_name,
                                           .type = import.module_path + "." + klass.name,
                                           .location = import.location});
            module.native_types.push_back({.name = import.module_path + "." + klass.name,
                                           .type = import.module_path + "." + klass.name,
                                           .location = import.location});
            module.native_classes.push_back(
                imported_class_shape(klass, exposed_name, type_substitutions, import.location));
            module.native_classes.push_back(imported_class_shape(
                klass, import.module_path + "." + klass.name, type_substitutions, import.location));
            return;
        }
    }
    for (const ConstDecl& constant : dependency.constants) {
        if (constant.name == import.imported_name) {
            const TypeRef value_type = substitute_type_ref(constant.type_ref, type_substitutions);
            module.native_values.push_back({.name = exposed_name,
                                            .type = substitute_type_ref_text(value_type, {}),
                                            .type_ref = value_type,
                                            .location = import.location});
            return;
        }
    }
    for (const FunctionDecl& fn : dependency.functions) {
        if (fn.name == import.imported_name) {
            add_function_alias(module, fn, exposed_name, type_substitutions, import.location);
            return;
        }
    }
}

} // namespace dudu
