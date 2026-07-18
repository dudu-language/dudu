#include "dudu/project/module_import_aliases.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/generic_param_metadata.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace dudu {
namespace {

TypeRef module_qualified_type_ref(const std::string& prefix, const std::string& name,
                                  const SourceLocation& location) {
    return named_type_ref(prefix + "." + name, location);
}

NativeSymbolId module_symbol_identity(std::string canonical_path) {
    NativeSymbolId id;
    id.canonical_path = std::move(canonical_path);
    return id;
}

void add_module_type_alias(ModuleAst& module, const std::string& prefix, const std::string& name,
                           const TypeRef& type, const SourceLocation& location,
                           std::string canonical_path, std::string doc_comment = {}) {
    const std::string exposed_name = prefix + "." + name;
    module.native_types.push_back({.name = prefix + "." + name,
                                   .native_spelling = "",
                                   .type_ref = type,
                                   .identity = module_symbol_identity(std::move(canonical_path)),
                                   .location = location,
                                   .doc_comment = std::move(doc_comment)});
}

std::string declaration_path(const std::string& origin_module, const std::string& dependency_module,
                             const std::string& name) {
    return (origin_module.empty() ? dependency_module : origin_module) + "." + name;
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
                ? named_type_ref(exposed_name, import.location)
                : module_qualified_type_ref(import.module_path, alias.name, import.location);
    }
    for (const EnumDecl& en : dependency.enums) {
        out[en.name] =
            en.name == import.imported_name
                ? named_type_ref(exposed_name, import.location)
                : module_qualified_type_ref(import.module_path, en.name, import.location);
    }
    for (const ClassDecl& klass : dependency.classes) {
        out[klass.name] =
            klass.name == import.imported_name
                ? named_type_ref(exposed_name, import.location)
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

void collect_type_names(const TypeRef& type, std::set<std::string>& names) {
    const std::string name = type_ref_head_name(type);
    if (!name.empty()) {
        names.insert(name);
    }
    for (const TypeRef& child : type.children) {
        collect_type_names(child, names);
    }
}

bool has_native_class(const ModuleAst& module, const ClassDecl& candidate) {
    return std::any_of(
        module.native_classes.begin(), module.native_classes.end(), [&](const ClassDecl& klass) {
            return klass.name == candidate.name && klass.identity.usr == candidate.identity.usr &&
                   klass.identity.canonical_path == candidate.identity.canonical_path;
        });
}

bool has_imported_enum_shape(const ModuleAst& module, const EnumDecl& candidate) {
    return std::any_of(module.imported_enum_shapes.begin(), module.imported_enum_shapes.end(),
                       [&](const EnumDecl& en) { return en.name == candidate.name; });
}

void collect_function_types(const FunctionDecl& fn, std::set<std::string>& names) {
    collect_type_names(fn.receiver_type_ref, names);
    for (const ParamDecl& param : fn.params) {
        collect_type_names(param.type_ref, names);
    }
    collect_type_names(fn.return_type_ref, names);
}

void collect_class_types(const ClassDecl& klass, std::set<std::string>& names) {
    for (const BaseClassDecl& base : klass.base_class_refs) {
        collect_type_names(base.type_ref, names);
    }
    for (const FieldDecl& field : klass.fields) {
        collect_type_names(field.type_ref, names);
    }
    for (const FunctionDecl& method : klass.methods) {
        collect_function_types(method, names);
    }
}

void add_signature_type_support(ModuleAst& module, const ModuleAst& dependency,
                                const NativeFunctionDecl& signature) {
    std::set<std::string> pending;
    for (const TypeRef& param : signature.param_type_refs) {
        collect_type_names(param, pending);
    }
    collect_type_names(signature.return_type_ref, pending);

    std::set<std::string> visited;
    while (!pending.empty()) {
        const std::string name = *pending.begin();
        pending.erase(pending.begin());
        if (!visited.insert(name).second) {
            continue;
        }
        for (const ClassDecl& klass : dependency.native_classes) {
            if (klass.name != name) {
                continue;
            }
            if (!has_native_class(module, klass)) {
                module.native_classes.push_back(klass);
            }
            collect_class_types(klass, pending);
        }
        for (const EnumDecl& en : dependency.imported_enum_shapes) {
            if (en.name != name) {
                continue;
            }
            if (!has_imported_enum_shape(module, en)) {
                module.imported_enum_shapes.push_back(en);
            }
            for (const EnumValueDecl& value : en.values) {
                for (const EnumPayloadField& field : value.payload_fields) {
                    collect_type_names(field.type_ref, pending);
                }
            }
            for (const FunctionDecl& method : en.methods) {
                collect_function_types(method, pending);
            }
        }
    }
}

void add_function_alias(ModuleAst& module, const FunctionDecl& fn, const std::string& name,
                        const ModuleAst& dependency,
                        const std::map<std::string, TypeRef>& type_substitutions,
                        const SourceLocation& location) {
    NativeFunctionDecl alias;
    alias.name = name;
    alias.template_params = fn.generic_params;
    alias.template_param_is_value = fn.generic_param_is_value.size() == fn.generic_params.size()
                                        ? fn.generic_param_is_value
                                        : infer_function_generic_param_value_flags(fn);
    alias.identity =
        module_symbol_identity(fn.origin_module.empty() ? name : fn.origin_module + "." + fn.name);
    alias.return_type_ref = function_has_return_type(fn)
                                ? substitute_type_ref(fn.return_type_ref, type_substitutions)
                                : void_type_ref(location);
    alias.location = location;
    alias.doc_comment = fn.doc_comment;
    for (size_t index = 0; index < fn.params.size(); ++index) {
        const ParamDecl& param = fn.params[index];
        alias.param_names.push_back(param.name);
        TypeRef param_type = substitute_type_ref(param.type_ref, type_substitutions);
        alias.param_type_refs.push_back(std::move(param_type));
        if (param.variadic) {
            alias.variadic = true;
            alias.min_params = static_cast<int>(index);
        }
    }
    module.native_functions.push_back(std::move(alias));
    add_signature_type_support(module, dependency, module.native_functions.back());
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
        klass.identity = module_symbol_identity(
            klass.origin_module.empty() ? name : klass.origin_module + "." + klass.name);
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

EnumDecl imported_enum_shape(EnumDecl en, const std::string& name,
                             const std::map<std::string, TypeRef>& type_substitutions,
                             const SourceLocation& location) {
    en.name = name;
    en.location = location;
    for (EnumValueDecl& value : en.values) {
        for (EnumPayloadField& field : value.payload_fields) {
            field.type_ref = substitute_type_ref(field.type_ref, type_substitutions);
        }
    }
    return en;
}

} // namespace

bool selective_module_symbol_already_projected(const ModuleAst& module, const ModuleAst& dependency,
                                               const ImportDecl& import) {
    const std::string name = import.alias.empty() ? import.imported_name : import.alias;
    std::string identity;
    for (const TypeAliasDecl& alias : dependency.aliases) {
        if (alias.name == import.imported_name) {
            identity = declaration_path(alias.origin_module, dependency.module_path, alias.name);
        }
    }
    for (const EnumDecl& en : dependency.enums) {
        if (en.name == import.imported_name) {
            identity = declaration_path(en.origin_module, dependency.module_path, en.name);
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        if (klass.name == import.imported_name) {
            identity = declaration_path(klass.origin_module, dependency.module_path, klass.name);
        }
    }
    for (const FunctionDecl& fn : dependency.functions) {
        if (fn.name == import.imported_name) {
            identity = declaration_path(fn.origin_module, dependency.module_path, fn.name);
        }
    }
    for (const ConstDecl& constant : dependency.constants) {
        if (constant.name == import.imported_name) {
            identity =
                declaration_path(constant.origin_module, dependency.module_path, constant.name);
        }
    }
    if (identity.empty()) {
        return false;
    }
    for (const NativeTypeDecl& type : module.native_types) {
        if (type.name == name && type.identity.canonical_path == identity) {
            return true;
        }
    }
    for (const ClassDecl& klass : module.native_classes) {
        if (klass.name == name && klass.identity.canonical_path == identity) {
            return true;
        }
    }
    for (const NativeValueDecl& value : module.native_values) {
        if (value.name == name && value.identity.canonical_path == identity) {
            return true;
        }
    }
    for (const NativeFunctionDecl& fn : module.native_functions) {
        if (fn.name == name && fn.identity.canonical_path == identity) {
            return true;
        }
    }
    return false;
}

void add_qualified_module_symbols(ModuleAst& module, const ModuleAst& dependency,
                                  const ImportDecl& import) {
    const std::string prefix = import.alias.empty() ? import.module_path : import.alias;
    if (prefix.empty()) {
        return;
    }
    const std::map<std::string, TypeRef> type_substitutions =
        qualified_type_substitutions(dependency, prefix, import.location);
    module.module_strip_prefixes.push_back(prefix);
    module.module_import_prefixes.push_back(prefix);
    for (const TypeAliasDecl& alias : dependency.aliases) {
        add_module_type_alias(
            module, prefix, alias.name, substitute_type_ref(alias.type_ref, type_substitutions),
            import.location,
            declaration_path(alias.origin_module, dependency.module_path, alias.name),
            alias.doc_comment);
    }
    for (const EnumDecl& en : dependency.enums) {
        module.imported_enum_shapes.push_back(
            imported_enum_shape(en, prefix + "." + en.name, type_substitutions, import.location));
        add_module_type_alias(
            module, prefix, en.name, module_qualified_type_ref(prefix, en.name, import.location),
            import.location, declaration_path(en.origin_module, dependency.module_path, en.name),
            en.doc_comment);
        for (const EnumValueDecl& value : en.values) {
            const std::string exposed_name = prefix + "." + en.name + "." + value.name;
            module.native_values.push_back(
                {.name = exposed_name,
                 .native_spelling = "",
                 .type_ref = module_qualified_type_ref(prefix, en.name, import.location),
                 .enum_constant = true,
                 .identity = module_symbol_identity(
                     en.origin_module.empty()
                         ? dependency.module_path + "." + en.name + "." + value.name
                         : en.origin_module + "." + en.name + "." + value.name),
                 .location = import.location,
                 .doc_comment = value.doc_comment});
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        add_module_type_alias(
            module, prefix, klass.name,
            module_qualified_type_ref(prefix, klass.name, import.location), import.location,
            declaration_path(klass.origin_module, dependency.module_path, klass.name),
            klass.doc_comment);
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
             .location = import.location,
             .doc_comment = constant.doc_comment});
    }
    for (const FunctionDecl& fn : dependency.functions) {
        add_function_alias(module, fn, prefix + "." + fn.name, dependency, type_substitutions,
                           import.location);
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
                import.location,
                declaration_path(en.origin_module, dependency.module_path, en.name),
                en.doc_comment);
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        if (klass.name != import.imported_name) {
            add_module_type_alias(
                module, import.module_path, klass.name,
                module_qualified_type_ref(import.module_path, klass.name, import.location),
                import.location,
                declaration_path(klass.origin_module, dependency.module_path, klass.name),
                klass.doc_comment);
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
                 .location = import.location,
                 .doc_comment = alias.doc_comment});
            return;
        }
    }
    for (const EnumDecl& en : dependency.enums) {
        if (en.name == import.imported_name) {
            module.imported_enum_shapes.push_back(
                imported_enum_shape(en, exposed_name, type_substitutions, import.location));
            module.native_types.push_back(
                {.name = exposed_name,
                 .native_spelling = "",
                 .type_ref =
                     module_qualified_type_ref(import.module_path, en.name, import.location),
                 .identity = module_symbol_identity(en.origin_module.empty()
                                                        ? import.module_path + "." + en.name
                                                        : en.origin_module + "." + en.name),
                 .location = import.location,
                 .doc_comment = en.doc_comment});
            add_module_type_alias(
                module, import.module_path, en.name,
                module_qualified_type_ref(import.module_path, en.name, import.location),
                import.location,
                declaration_path(en.origin_module, dependency.module_path, en.name),
                en.doc_comment);
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
                 .location = import.location,
                 .doc_comment = klass.doc_comment});
            add_module_type_alias(
                module, import.module_path, klass.name,
                module_qualified_type_ref(import.module_path, klass.name, import.location),
                import.location,
                declaration_path(klass.origin_module, dependency.module_path, klass.name),
                klass.doc_comment);
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
                 .location = import.location,
                 .doc_comment = constant.doc_comment});
            return;
        }
    }
    bool added_function = false;
    for (const FunctionDecl& fn : dependency.functions) {
        if (fn.name == import.imported_name) {
            add_function_alias(module, fn, exposed_name, dependency, type_substitutions,
                               import.location);
            added_function = true;
        }
    }
    if (added_function) {
        return;
    }
}

} // namespace dudu
