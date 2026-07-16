#include "dudu/codegen/cpp_module_emit_context.hpp"

#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/codegen/cpp_emit_prelude.hpp"
#include "dudu/core/ast_type.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

bool public_abi_function(const FunctionDecl& fn, bool test_source) {
    return fn.visibility != Visibility::Private && (test_source || !cpp_emit_function_is_test(fn));
}

std::string module_target_kind(const ModuleAst& module) {
    const auto found = module.build_values.find("TARGET_KIND");
    if (found == module.build_values.end()) {
        return {};
    }
    std::string value = found->second;
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

void add_local_generated_names(CppEmitOptions& options, const ModuleAst& unit, bool public_abi,
                               bool test_source) {
    for (const TypeAliasDecl& alias : unit.aliases) {
        if (!alias.cpp_name.empty()) {
            options.generated_type_names[alias.name] = alias.cpp_name;
        }
    }
    for (const EnumDecl& en : unit.enums) {
        if (!en.cpp_name.empty()) {
            options.generated_type_names[en.name] = en.cpp_name;
            options.generated_value_names[en.name] = en.cpp_name;
        }
    }
    for (const ClassDecl& klass : unit.classes) {
        if (!klass.cpp_name.empty()) {
            options.generated_type_names[klass.name] = klass.cpp_name;
            options.generated_value_names[klass.name] = klass.cpp_name;
        }
        for (const ConstDecl& constant : klass.constants) {
            if (!constant.cpp_name.empty()) {
                options.generated_value_names[klass.name + "." + constant.name] = constant.cpp_name;
            }
        }
        for (const FunctionDecl& method : klass.methods) {
            if (!method.cpp_name.empty() && cpp_reserved_identifier(method.name) &&
                !cpp_emit_function_has_decorator(method, "operator")) {
                options.generated_value_names[klass.name + "." + method.name] = method.cpp_name;
                if (!klass.cpp_name.empty()) {
                    options.generated_value_names[klass.cpp_name + "." + method.name] =
                        method.cpp_name;
                }
            }
        }
    }
    for (const ConstDecl& constant : unit.constants) {
        if (!constant.cpp_name.empty()) {
            options.generated_value_names[constant.name] = constant.cpp_name;
        }
    }
    for (const FunctionDecl& fn : unit.functions) {
        if (!fn.cpp_name.empty()) {
            options.generated_value_names[fn.name] =
                cpp_emit_function_has_decorator(fn, "extern_c") ||
                        (public_abi && public_abi_function(fn, test_source))
                    ? fn.name
                    : fn.cpp_name;
        }
    }
}

void add_reserved_method_generated_names(CppEmitOptions& options, const std::string& type_name,
                                         const ClassDecl& klass) {
    for (const FunctionDecl& method : klass.methods) {
        if (!method.cpp_name.empty() && cpp_reserved_identifier(method.name) &&
            !cpp_emit_function_has_decorator(method, "operator")) {
            options.generated_value_names[type_name + "." + method.name] = method.cpp_name;
        }
    }
}

std::vector<std::string> stripped_alias_prefixes(const ModuleAst& unit) {
    std::vector<std::string> out;
    for (const std::string& alias : namespace_aliases(unit)) {
        if (!alias.empty() && alias.front() == '!') {
            out.push_back(alias.substr(1));
        }
    }
    return out;
}

std::optional<std::string> strip_native_alias_prefix(const std::string& name,
                                                     const std::vector<std::string>& aliases) {
    for (const std::string& alias : aliases) {
        const std::string marker = alias + ".";
        if (name.starts_with(marker)) {
            return name.substr(marker.size());
        }
    }
    return std::nullopt;
}

std::string native_alias_emit_name(const NativeTypeDecl& type, const std::string& stripped_name) {
    if (type.native_spelling.starts_with("struct ") || type.native_spelling.starts_with("union ") ||
        type.native_spelling.starts_with("enum ")) {
        return type.native_spelling;
    }
    return stripped_name;
}

void add_native_generated_names(CppEmitOptions& options, const ModuleAst& unit) {
    const std::vector<std::string> aliases = stripped_alias_prefixes(unit);
    if (aliases.empty()) {
        return;
    }
    for (const NativeTypeDecl& type : unit.native_types) {
        if (const auto name = strip_native_alias_prefix(type.name, aliases)) {
            options.generated_type_names[type.name] = native_alias_emit_name(type, *name);
        }
    }
    for (const ClassDecl& klass : unit.native_classes) {
        if (const auto name = strip_native_alias_prefix(klass.name, aliases)) {
            const std::string emitted = klass.cpp_name.empty() ? *name : klass.cpp_name;
            options.generated_type_names[klass.name] = emitted;
            options.generated_value_names[klass.name] = emitted;
        }
    }
    for (const NativeValueDecl& value : unit.native_values) {
        if (const auto name = strip_native_alias_prefix(value.name, aliases)) {
            options.generated_value_names[value.name] = *name;
        }
    }
    for (const NativeFunctionDecl& fn : unit.native_functions) {
        if (const auto name = strip_native_alias_prefix(fn.name, aliases)) {
            options.generated_value_names[fn.name] = *name;
        }
    }
    for (const NativeMacroDecl& macro : unit.native_macros) {
        if (const auto name = strip_native_alias_prefix(macro.name, aliases)) {
            options.generated_value_names[macro.name] = *name;
        }
    }
}

const ClassDecl* dependency_class_named(const ModuleAst& dependency, const std::string& name) {
    for (const ClassDecl& klass : dependency.classes) {
        if (klass.name == name) {
            return &klass;
        }
    }
    return nullptr;
}

void add_imported_generated_names(CppEmitOptions& options, const ModuleAst& dependency,
                                  const ImportDecl& import, bool public_abi, bool test_source) {
    const std::string prefix = import.alias.empty() ? import.module_path : import.alias;
    const bool selective = import.kind == ImportKind::From;
    const std::string selective_name = import.alias.empty() ? import.imported_name : import.alias;
    auto expose = [&](const std::string& name) {
        return selective ? selective_name : prefix + "." + name;
    };
    for (const TypeAliasDecl& alias : dependency.aliases) {
        if ((!selective || alias.name == import.imported_name) && !alias.cpp_name.empty()) {
            options.generated_type_names[expose(alias.name)] = alias.cpp_name;
        }
    }
    for (const EnumDecl& en : dependency.enums) {
        if ((!selective || en.name == import.imported_name) && !en.cpp_name.empty()) {
            options.generated_type_names[expose(en.name)] = en.cpp_name;
            options.generated_value_names[expose(en.name)] = en.cpp_name;
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        if ((!selective || klass.name == import.imported_name) && !klass.cpp_name.empty()) {
            options.generated_type_names[expose(klass.name)] = klass.cpp_name;
            options.generated_value_names[expose(klass.name)] = klass.cpp_name;
            for (const FunctionDecl& method : klass.methods) {
                if (!method.cpp_name.empty() && cpp_reserved_identifier(method.name) &&
                    !cpp_emit_function_has_decorator(method, "operator")) {
                    options.generated_value_names[expose(klass.name) + "." + method.name] =
                        method.cpp_name;
                    options.generated_value_names[klass.cpp_name + "." + method.name] =
                        method.cpp_name;
                }
            }
        }
    }
    for (const ConstDecl& constant : dependency.constants) {
        if ((!selective || constant.name == import.imported_name) && !constant.cpp_name.empty()) {
            options.generated_value_names[expose(constant.name)] = constant.cpp_name;
            const std::string type_name = type_ref_head_name(constant.type_ref);
            if (const ClassDecl* klass = dependency_class_named(dependency, type_name)) {
                add_reserved_method_generated_names(options, import.module_path + "." + type_name,
                                                    *klass);
            }
        }
    }
    for (const FunctionDecl& fn : dependency.functions) {
        if ((!selective || fn.name == import.imported_name) && !fn.cpp_name.empty()) {
            options.generated_value_names[expose(fn.name)] =
                cpp_emit_function_has_decorator(fn, "extern_c") ||
                        (public_abi && public_abi_function(fn, test_source))
                    ? fn.name
                    : fn.cpp_name;
        }
    }
}

} // namespace

bool preserve_public_abi_names(const ModuleAst& module) {
    const std::string kind = module_target_kind(module);
    return kind == "library" || kind == "shared_library";
}

std::string resolved_module_path_for_import(const ModuleAst& unit, const ImportDecl& import) {
    for (const ModuleDependency& dependency : unit.dependencies) {
        if (dependency.kind == import.kind && dependency.import_module_path == import.module_path &&
            dependency.location.line == import.location.line &&
            dependency.location.column == import.location.column) {
            return dependency.resolved_module_path;
        }
    }
    return import.module_path;
}

CppEmitOptions make_cpp_module_emit_options(const ModuleAst& unit, const CppModuleMap& modules,
                                            bool test_source, bool public_abi,
                                            bool include_macro_host_modules) {
    CppEmitOptions options;
    options.emit_prelude = false;
    options.use_generated_names = true;
    options.test_source = test_source;
    options.expose_test_functions = test_source;
    add_local_generated_names(options, unit, public_abi, test_source);
    add_native_generated_names(options, unit);
    for (const ImportDecl& import : unit.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const auto dependency = modules.find(resolved_module_path_for_import(unit, import));
        if (dependency != modules.end() &&
            (include_macro_host_modules ||
             dependency->second->compilation_domain != CompilationDomain::MacroHost)) {
            add_imported_generated_names(options, *dependency->second, import, public_abi,
                                         test_source);
        }
    }
    return options;
}

} // namespace dudu
