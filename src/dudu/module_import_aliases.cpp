#include "dudu/module_import_aliases.hpp"

#include "dudu/ast_type.hpp"

#include <map>

namespace dudu {
namespace {

TypeRef module_named_type_ref(const std::string& name, const SourceLocation& location) {
    return named_type_ref(name, location);
}

TypeRef module_qualified_type_ref(const std::string& prefix, const std::string& name,
                                  const SourceLocation& location) {
    return module_named_type_ref(prefix + "." + name, location);
}

NativeSymbolId module_symbol_identity(std::string canonical_path) {
    NativeSymbolId id;
    id.canonical_path = std::move(canonical_path);
    return id;
}

void add_module_type_alias(ModuleAst& module, const std::string& prefix, const std::string& name,
                           const TypeRef& type, const SourceLocation& location) {
    const std::string exposed_name = prefix + "." + name;
    module.native_types.push_back({.name = prefix + "." + name,
                                   .native_spelling = "",
                                   .type_ref = type,
                                   .identity = module_symbol_identity(exposed_name),
                                   .location = location});
}

std::map<std::string, TypeRef> qualified_type_substitutions(const ModuleAst& dependency,
                                                            const std::string& prefix,
                                                            const SourceLocation& location) {
    std::map<std::string, TypeRef> out;
    for (const TypeAliasDecl& alias : dependency.aliases) {
        out[alias.name] = module_qualified_type_ref(prefix, alias.name, location);
    }
    for (const EnumDecl& en : dependency.enums) {
        out[en.name] = module_qualified_type_ref(prefix, en.name, location);
    }
    for (const ClassDecl& klass : dependency.classes) {
        out[klass.name] = module_qualified_type_ref(prefix, klass.name, location);
    }
    return out;
}

std::map<std::string, TypeRef> selective_type_substitutions(const ModuleAst& dependency,
                                                            const ImportDecl& import) {
    const std::string exposed_name = import.alias.empty() ? import.imported_name : import.alias;
    std::map<std::string, TypeRef> out;
    for (const TypeAliasDecl& alias : dependency.aliases) {
        out[alias.name] =
            alias.name == import.imported_name
                ? module_named_type_ref(exposed_name, import.location)
                : module_qualified_type_ref(import.module_path, alias.name, import.location);
    }
    for (const EnumDecl& en : dependency.enums) {
        out[en.name] =
            en.name == import.imported_name
                ? module_named_type_ref(exposed_name, import.location)
                : module_qualified_type_ref(import.module_path, en.name, import.location);
    }
    for (const ClassDecl& klass : dependency.classes) {
        out[klass.name] =
            klass.name == import.imported_name
                ? module_named_type_ref(exposed_name, import.location)
                : module_qualified_type_ref(import.module_path, klass.name, import.location);
    }
    for (const NativeTypeDecl& type : dependency.native_types) {
        if (type.name.find('.') == std::string::npos &&
            (has_type_ref(type.type_ref) || !type.native_spelling.empty())) {
            out[type.name] = native_type_alias_type_ref(type);
        }
    }
    return out;
}

void add_function_alias(ModuleAst& module, const FunctionDecl& fn, const std::string& name,
                        const std::map<std::string, TypeRef>& type_substitutions,
                        const SourceLocation& location) {
    NativeFunctionDecl alias;
    alias.name = name;
    alias.template_params = fn.generic_params;
    alias.identity =
        module_symbol_identity(fn.origin_module.empty() ? name : fn.origin_module + "." + fn.name);
    alias.return_type_ref = function_has_return_type(fn)
                                ? substitute_type_ref(fn.return_type_ref, type_substitutions)
                                : void_type_ref(location);
    alias.location = location;
    for (const ParamDecl& param : fn.params) {
        TypeRef param_type = substitute_type_ref(param.type_ref, type_substitutions);
        alias.param_type_refs.push_back(std::move(param_type));
    }
    module.native_functions.push_back(std::move(alias));
}

FunctionDecl substituted_method(FunctionDecl method,
                                const std::map<std::string, TypeRef>& type_substitutions) {
    if (function_has_return_type(method)) {
        method.return_type_ref = substitute_type_ref(method.return_type_ref, type_substitutions);
    }
    for (ParamDecl& param : method.params) {
        param.type_ref = substitute_type_ref(param.type_ref, type_substitutions);
    }
    return method;
}

ClassDecl imported_class_shape(ClassDecl klass, const std::string& name,
                               const std::map<std::string, TypeRef>& type_substitutions,
                               const SourceLocation& location) {
    if (klass.identity.canonical_path.empty() && klass.identity.usr.empty()) {
        klass.identity = module_symbol_identity(klass.origin_module.empty() ? name
                                                                            : klass.origin_module);
    }
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
    const std::map<std::string, TypeRef> type_substitutions =
        qualified_type_substitutions(dependency, prefix, import.location);
    module.module_strip_prefixes.push_back(prefix);
    for (const TypeAliasDecl& alias : dependency.aliases) {
        add_module_type_alias(module, prefix, alias.name,
                              substitute_type_ref(alias.type_ref, type_substitutions),
                              import.location);
    }
    for (const EnumDecl& en : dependency.enums) {
        add_module_type_alias(module, prefix, en.name,
                              module_qualified_type_ref(prefix, en.name, import.location),
                              import.location);
    }
    for (const ClassDecl& klass : dependency.classes) {
        add_module_type_alias(module, prefix, klass.name,
                              module_qualified_type_ref(prefix, klass.name, import.location),
                              import.location);
        module.native_classes.push_back(imported_class_shape(klass, prefix + "." + klass.name,
                                                             type_substitutions, import.location));
    }
    for (const ConstDecl& constant : dependency.constants) {
        const TypeRef value_type = substitute_type_ref(constant.type_ref, type_substitutions);
        module.native_values.push_back(
            {.name = prefix + "." + constant.name,
             .native_spelling = "",
             .type_ref = value_type,
             .identity = module_symbol_identity(constant.origin_module.empty()
                                                    ? prefix + "." + constant.name
                                                    : constant.origin_module + "." + constant.name),
             .location = import.location});
    }
    for (const FunctionDecl& fn : dependency.functions) {
        add_function_alias(module, fn, prefix + "." + fn.name, type_substitutions, import.location);
    }
}

void add_selective_module_symbol(ModuleAst& module, const ModuleAst& dependency,
                                 const ImportDecl& import) {
    const std::string exposed_name = import.alias.empty() ? import.imported_name : import.alias;
    const std::map<std::string, TypeRef> type_substitutions =
        selective_type_substitutions(dependency, import);
    module.module_strip_prefixes.push_back(import.module_path);
    for (const EnumDecl& en : dependency.enums) {
        if (en.name != import.imported_name) {
            add_module_type_alias(
                module, import.module_path, en.name,
                module_qualified_type_ref(import.module_path, en.name, import.location),
                import.location);
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        if (klass.name != import.imported_name) {
            add_module_type_alias(
                module, import.module_path, klass.name,
                module_qualified_type_ref(import.module_path, klass.name, import.location),
                import.location);
            module.native_classes.push_back(imported_class_shape(
                klass, import.module_path + "." + klass.name, type_substitutions, import.location));
        }
    }
    for (const TypeAliasDecl& alias : dependency.aliases) {
        if (alias.name == import.imported_name) {
            const TypeRef alias_type = substitute_type_ref(alias.type_ref, type_substitutions);
            module.native_types.push_back(
                {.name = exposed_name,
                 .native_spelling = "",
                 .type_ref = alias_type,
                 .identity = module_symbol_identity(alias.origin_module.empty()
                                                        ? exposed_name
                                                        : alias.origin_module + "." + alias.name),
                 .location = import.location});
            return;
        }
    }
    for (const EnumDecl& en : dependency.enums) {
        if (en.name == import.imported_name) {
            module.native_types.push_back(
                {.name = exposed_name,
                 .native_spelling = "",
                 .type_ref =
                     module_qualified_type_ref(import.module_path, en.name, import.location),
                 .identity = module_symbol_identity(en.origin_module.empty()
                                                        ? import.module_path + "." + en.name
                                                        : en.origin_module + "." + en.name),
                 .location = import.location});
            add_module_type_alias(
                module, import.module_path, en.name,
                module_qualified_type_ref(import.module_path, en.name, import.location),
                import.location);
            return;
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        if (klass.name == import.imported_name) {
            module.native_types.push_back(
                {.name = exposed_name,
                 .native_spelling = "",
                 .type_ref =
                     module_qualified_type_ref(import.module_path, klass.name, import.location),
                 .identity = module_symbol_identity(klass.origin_module.empty()
                                                        ? import.module_path + "." + klass.name
                                                        : klass.origin_module + "." + klass.name),
                 .location = import.location});
            add_module_type_alias(
                module, import.module_path, klass.name,
                module_qualified_type_ref(import.module_path, klass.name, import.location),
                import.location);
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
            module.native_values.push_back(
                {.name = exposed_name,
                 .native_spelling = "",
                 .type_ref = value_type,
                 .identity = module_symbol_identity(
                     constant.origin_module.empty() ? exposed_name
                                                    : constant.origin_module + "." + constant.name),
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
