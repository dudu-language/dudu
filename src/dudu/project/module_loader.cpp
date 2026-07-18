#include "dudu/project/module_loader.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/file_io.hpp"
#include "dudu/core/source.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_import_aliases.hpp"
#include "dudu/project/module_names.hpp"
#include "dudu/project/standard_library.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

std::string read_text_file(const std::filesystem::path& path) {
    if (std::optional<std::string> text = try_read_text_file(path)) {
        return std::move(*text);
    }
    throw CompileError({.file = SourceFileName(path.string()), .line = 1, .column = 1},
                       "could not open module");
}

std::string source_digest(std::string_view source) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : source) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

std::filesystem::path canonical_source_path(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

bool path_is_under(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path relative = std::filesystem::relative(path, root);
    return relative.empty() || (relative.begin() != relative.end() && *relative.begin() != "..");
}

std::string import_package_name(const std::string& module_path) {
    const size_t dot = module_path.find('.');
    return dot == std::string::npos ? module_path : module_path.substr(0, dot);
}

std::filesystem::path
module_root_for_source(const std::filesystem::path& entry_root, const std::filesystem::path& source,
                       const std::map<std::string, std::filesystem::path>& module_roots) {
    std::filesystem::path best;
    size_t best_size = 0;
    if (path_is_under(source, entry_root)) {
        best = entry_root;
        best_size = best.string().size();
    }
    for (const auto& [_, root] : module_roots) {
        if (path_is_under(source, root) && root.string().size() >= best_size) {
            best = root;
            best_size = root.string().size();
        }
    }
    return best.empty() ? source.parent_path() : best;
}

std::filesystem::path
resolve_import_path(const std::filesystem::path& current_path, const std::filesystem::path& root,
                    const std::string& module_path,
                    const std::map<std::string, std::filesystem::path>& module_roots) {
    const std::filesystem::path local =
        module_path_to_file(current_path.parent_path(), module_path);
    if (std::filesystem::exists(local)) {
        return local;
    }
    const auto found = module_roots.find(import_package_name(module_path));
    if (found == module_roots.end()) {
        const std::filesystem::path rooted = module_path_to_file(root, module_path);
        if (std::filesystem::exists(rooted)) {
            return rooted;
        }
        return local;
    }
    return module_path_to_file(found->second, module_path);
}

void stamp_module_origin(ModuleAst& module, const std::filesystem::path& source_path,
                         const std::string& module_path) {
    module.source_path = source_path;
    module.module_path = module_path;
    for (TypeAliasDecl& alias : module.aliases) {
        alias.origin_module = module_path;
        alias.cpp_name = generated_type_name(module_path, alias.name);
    }
    for (EnumDecl& en : module.enums) {
        en.origin_module = module_path;
        en.cpp_name = generated_type_name(module_path, en.name);
        for (FunctionDecl& method : en.methods) {
            method.origin_module = module_path;
            method.cpp_name = generated_value_name(module_path, en.name + "_" + method.name);
        }
    }
    for (ClassDecl& klass : module.classes) {
        klass.origin_module = module_path;
        klass.cpp_name = generated_type_name(module_path, klass.name);
        for (ConstDecl& constant : klass.constants) {
            constant.origin_module = module_path;
            constant.cpp_name = generated_value_name(module_path, klass.name + "_" + constant.name);
        }
        for (ConstDecl& field : klass.static_fields) {
            field.origin_module = module_path;
            field.cpp_name = generated_value_name(module_path, klass.name + "_" + field.name);
        }
        for (FunctionDecl& method : klass.methods) {
            method.origin_module = module_path;
            method.cpp_name = generated_value_name(module_path, klass.name + "_" + method.name);
        }
    }
    for (ConstDecl& constant : module.constants) {
        constant.origin_module = module_path;
        constant.cpp_name = generated_value_name(module_path, constant.name);
    }
    for (FunctionDecl& fn : module.functions) {
        fn.origin_module = module_path;
        fn.cpp_name = generated_value_name(module_path, fn.name);
    }
}

void append_module(ModuleAst& target, const ModuleAst& source) {
    target.imports.insert(target.imports.end(), source.imports.begin(), source.imports.end());
    target.aliases.insert(target.aliases.end(), source.aliases.begin(), source.aliases.end());
    target.native_types.insert(target.native_types.end(), source.native_types.begin(),
                               source.native_types.end());
    target.native_values.insert(target.native_values.end(), source.native_values.begin(),
                                source.native_values.end());
    target.native_functions.insert(target.native_functions.end(), source.native_functions.begin(),
                                   source.native_functions.end());
    target.native_macros.insert(target.native_macros.end(), source.native_macros.begin(),
                                source.native_macros.end());
    target.native_namespaces.insert(target.native_namespaces.end(),
                                    source.native_namespaces.begin(),
                                    source.native_namespaces.end());
    target.native_classes.insert(target.native_classes.end(), source.native_classes.begin(),
                                 source.native_classes.end());
    target.module_strip_prefixes.insert(target.module_strip_prefixes.end(),
                                        source.module_strip_prefixes.begin(),
                                        source.module_strip_prefixes.end());
    target.module_import_prefixes.insert(target.module_import_prefixes.end(),
                                         source.module_import_prefixes.begin(),
                                         source.module_import_prefixes.end());
    target.resolved_macro_decorators.insert(source.resolved_macro_decorators.begin(),
                                            source.resolved_macro_decorators.end());
    target.generated_origins.insert(target.generated_origins.end(),
                                    source.generated_origins.begin(),
                                    source.generated_origins.end());
    target.enums.insert(target.enums.end(), source.enums.begin(), source.enums.end());
    target.classes.insert(target.classes.end(), source.classes.begin(), source.classes.end());
    target.constants.insert(target.constants.end(), source.constants.begin(),
                            source.constants.end());
    target.functions.insert(target.functions.end(), source.functions.begin(),
                            source.functions.end());
    target.static_asserts.insert(target.static_asserts.end(), source.static_asserts.begin(),
                                 source.static_asserts.end());
}

bool has_module_symbol(const ModuleAst& module, const std::string& name) {
    for (const TypeAliasDecl& alias : module.aliases)
        if (alias.name == name)
            return true;
    for (const EnumDecl& en : module.enums)
        if (en.name == name)
            return true;
    for (const ClassDecl& klass : module.classes)
        if (klass.name == name)
            return true;
    for (const FunctionDecl& fn : module.functions)
        if (fn.name == name)
            return true;
    for (const ConstDecl& constant : module.constants)
        if (constant.name == name)
            return true;
    return false;
}

bool has_native_binding(const ModuleAst& module, const std::string& name) {
    for (const NativeTypeDecl& type : module.native_types)
        if (type.name == name)
            return true;
    for (const NativeValueDecl& value : module.native_values)
        if (value.name == name)
            return true;
    for (const NativeFunctionDecl& fn : module.native_functions)
        if (fn.name == name)
            return true;
    for (const ClassDecl& klass : module.native_classes)
        if (klass.name == name)
            return true;
    return false;
}

bool has_binding(const ModuleAst& module, const std::string& name) {
    return has_module_symbol(module, name) || has_native_binding(module, name);
}

std::string selective_import_name(const ImportDecl& import) {
    return import.alias.empty() ? import.imported_name : import.alias;
}

void report_missing_import(const ImportDecl& import,
                           std::vector<ParseDiagnostic>* diagnostics) {
    const std::string message = "module '" + import.module_path + "' has no symbol '" +
                                import.imported_name + "'";
    if (diagnostics != nullptr) {
        diagnostics->push_back({.location = import.location,
                                .message = message,
                                .code = "dudu.sema.missing_import",
                                .data_name = import.imported_name});
        return;
    }
    throw CompileError(import.location, message, "dudu.sema.missing_import",
                       import.imported_name);
}

void report_missing_module(const ImportDecl& import,
                           std::vector<ParseDiagnostic>* diagnostics) {
    const std::string message = "could not resolve module '" + import.module_path + "'";
    if (diagnostics != nullptr) {
        diagnostics->push_back({.location = import.location,
                                .message = message,
                                .code = "dudu.sema.missing_module",
                                .data_name = import.module_path});
        return;
    }
    throw CompileError(import.location, message, "dudu.sema.missing_module",
                       import.module_path);
}

void reject_selective_import_collision(const ModuleAst& module, const ImportDecl& import) {
    const std::string name = selective_import_name(import);
    if (!has_binding(module, name)) {
        return;
    }
    throw CompileError(import.location,
                       "from import name '" + name +
                           "' collides with an existing declaration; use 'as' to choose a "
                           "unique local name");
}

void add_import_aliases_for_unit(ModuleAst& module,
                                 const std::map<std::filesystem::path, ModuleAst>& loaded) {
    for (const ImportDecl& import : module.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        auto dependency_info =
            std::find_if(module.dependencies.begin(), module.dependencies.end(),
                         [&](const ModuleDependency& dependency) {
                             return dependency.kind == import.kind &&
                                    dependency.import_module_path == import.module_path;
                         });
        if (dependency_info == module.dependencies.end()) {
            continue;
        }
        const std::filesystem::path dependency_path =
            std::filesystem::weakly_canonical(dependency_info->source_path);
        const auto dependency = loaded.find(dependency_path);
        if (dependency == loaded.end()) {
            continue;
        }
        if (import.kind == ImportKind::Module) {
            add_qualified_module_symbols(module, dependency->second, import);
        } else {
            reject_selective_import_collision(module, import);
            add_selective_module_symbol(module, dependency->second, import);
        }
    }
}

void add_from_import_aliases(ModuleAst& module) {
    std::vector<TypeAliasDecl> type_aliases;
    std::vector<ConstDecl> const_aliases;
    std::vector<FunctionDecl> function_aliases;
    std::set<std::string> pending_aliases;

    for (const ImportDecl& import : module.imports) {
        if (import.kind != ImportKind::From || import.alias.empty() ||
            import.alias == import.imported_name) {
            continue;
        }
        if (has_module_symbol(module, import.alias) || pending_aliases.contains(import.alias)) {
            throw CompileError(import.location,
                               "import alias '" + import.alias + "' collides with a declaration");
        }
        pending_aliases.insert(import.alias);

        bool added = false;
        for (const TypeAliasDecl& alias : module.aliases) {
            if (alias.name == import.imported_name) {
                TypeAliasDecl copy = alias;
                copy.name = import.alias;
                copy.location = import.location;
                type_aliases.push_back(std::move(copy));
                added = true;
                break;
            }
        }
        if (added) {
            continue;
        }
        for (const EnumDecl& en : module.enums) {
            if (en.name == import.imported_name) {
                TypeAliasDecl alias;
                alias.name = import.alias;
                alias.type_ref = named_type_ref(import.imported_name, import.location);
                alias.location = import.location;
                type_aliases.push_back(std::move(alias));
                added = true;
                break;
            }
        }
        if (added) {
            continue;
        }
        for (const ClassDecl& klass : module.classes) {
            if (klass.name == import.imported_name) {
                TypeAliasDecl alias;
                alias.name = import.alias;
                alias.type_ref = named_type_ref(import.imported_name, import.location);
                alias.location = import.location;
                type_aliases.push_back(std::move(alias));
                added = true;
                break;
            }
        }
        if (added) {
            continue;
        }
        for (const ConstDecl& constant : module.constants) {
            if (constant.name == import.imported_name) {
                ConstDecl alias = constant;
                alias.name = import.alias;
                alias.value_expr = make_expr(ExprKind::Name, import.imported_name, import.location);
                alias.value_expr.name = import.imported_name;
                alias.location = import.location;
                const_aliases.push_back(std::move(alias));
                added = true;
                break;
            }
        }
        if (added) {
            continue;
        }
        for (const FunctionDecl& fn : module.functions) {
            if (fn.name == import.imported_name) {
                FunctionDecl alias = fn;
                alias.name = import.alias;
                alias.location = import.location;
                function_aliases.push_back(std::move(alias));
                added = true;
            }
        }
        // Strict loading already rejects a missing selective import. Recovering loading keeps
        // the current graph but deliberately omits aliases for unavailable declarations.
    }

    module.aliases.insert(module.aliases.end(), type_aliases.begin(), type_aliases.end());
    module.constants.insert(module.constants.end(), const_aliases.begin(), const_aliases.end());
    module.functions.insert(module.functions.end(), function_aliases.begin(),
                            function_aliases.end());
}

const ModuleAst& load_one(const std::filesystem::path& path, const std::filesystem::path& root,
                          const std::map<std::filesystem::path, std::string>& source_overrides,
                          const std::map<std::string, std::filesystem::path>& module_roots,
                          std::vector<std::filesystem::path>& stack,
                          std::map<std::filesystem::path, ModuleAst>& loaded,
                          std::vector<std::filesystem::path>& ordered,
                          std::vector<ParseDiagnostic>* diagnostics) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (loaded.contains(canonical)) {
        return loaded.at(canonical);
    }
    if (std::find(stack.begin(), stack.end(), canonical) != stack.end()) {
        throw CompileError({.file = SourceFileName(path.string()), .line = 1, .column = 1},
                           module_cycle_message(root, stack, canonical));
    }
    stack.push_back(canonical);

    const auto source_override = source_overrides.find(canonical);
    const std::string source =
        source_override == source_overrides.end() ? read_text_file(path) : source_override->second;
    ModuleAst parsed;
    if (diagnostics == nullptr) {
        parsed = parse_source(source, path);
    } else {
        ParseResult recovered = parse_source_recovering(source, path);
        parsed = std::move(recovered.module);
        diagnostics->insert(diagnostics->end(),
                            std::make_move_iterator(recovered.diagnostics.begin()),
                            std::make_move_iterator(recovered.diagnostics.end()));
    }
    stamp_module_origin(
        parsed, canonical,
        module_name_from_file(module_root_for_source(root, canonical, module_roots), canonical));
    parsed.source_digest = source_digest(source);
    for (const ImportDecl& import : parsed.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const std::filesystem::path dependency =
            resolve_import_path(path, root, import.module_path, module_roots);
        const std::filesystem::path canonical_dependency =
            canonical_source_path(dependency);
        if (!source_overrides.contains(canonical_dependency) &&
            !std::filesystem::exists(dependency)) {
            report_missing_module(import, diagnostics);
            continue;
        }
        if (std::find(stack.begin(), stack.end(), canonical_dependency) != stack.end()) {
            throw CompileError(import.location,
                               module_cycle_message(root, stack, canonical_dependency));
        }
        const ModuleAst& dependency_module = load_one(
            dependency, root, source_overrides, module_roots, stack, loaded, ordered, diagnostics);
        parsed.dependencies.push_back({.kind = import.kind,
                                       .import_module_path = import.module_path,
                                       .resolved_module_path = dependency_module.module_path,
                                       .source_path = dependency_module.source_path,
                                       .location = import.location});
        if (import.kind == ImportKind::From &&
            !has_module_symbol(dependency_module, import.imported_name)) {
            report_missing_import(import, diagnostics);
        }
    }

    stack.pop_back();
    loaded.emplace(canonical, std::move(parsed));
    ordered.push_back(canonical);
    return loaded.at(canonical);
}

void collect_files(const std::filesystem::path& path,
                   const std::map<std::string, std::filesystem::path>& module_roots,
                   std::vector<std::filesystem::path>& stack,
                   std::vector<std::filesystem::path>& out) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    const std::filesystem::path root =
        stack.empty() ? canonical.parent_path() : stack.front().parent_path();
    if (std::find(stack.begin(), stack.end(), canonical) != stack.end()) {
        throw CompileError({.file = SourceFileName(path.string()), .line = 1, .column = 1},
                           module_cycle_message(root, stack, canonical));
    }
    if (std::find(out.begin(), out.end(), canonical) != out.end()) {
        return;
    }
    stack.push_back(canonical);
    out.push_back(canonical);
    const ModuleAst parsed = parse_source(read_text_file(path), path);
    for (const ImportDecl& import : parsed.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const std::filesystem::path dependency =
            resolve_import_path(path, root, import.module_path, module_roots);
        const std::filesystem::path canonical_dependency =
            std::filesystem::weakly_canonical(dependency);
        if (std::find(stack.begin(), stack.end(), canonical_dependency) != stack.end()) {
            throw CompileError(import.location,
                               module_cycle_message(root, stack, canonical_dependency));
        }
        collect_files(dependency, module_roots, stack, out);
    }
    stack.pop_back();
}

} // namespace

LoadSourceTreeResult load_source_tree_impl(const LoadSourceTreeOptions& options, bool recovering) {
    const std::filesystem::path canonical_entry = std::filesystem::weakly_canonical(options.entry);
    const std::filesystem::path root = canonical_entry.parent_path();
    std::map<std::filesystem::path, std::string> canonical_overrides;
    for (const auto& [path, source] : options.source_overrides) {
        canonical_overrides[canonical_source_path(path)] = source;
    }
    std::map<std::string, std::filesystem::path> canonical_module_roots;
    for (const auto& [name, path] : with_standard_module_roots(options.module_roots)) {
        canonical_module_roots[name] = canonical_source_path(path);
    }
    std::vector<std::filesystem::path> stack;
    std::map<std::filesystem::path, ModuleAst> loaded;
    std::vector<std::filesystem::path> ordered;
    LoadSourceTreeResult result;
    (void)load_one(canonical_entry, root, canonical_overrides, canonical_module_roots, stack,
                   loaded, ordered, recovering ? &result.diagnostics : nullptr);

    ModuleAst& merged = result.module;
    merged.source_path = canonical_entry;
    merged.module_path = module_name_from_file(root, canonical_entry);
    for (const std::filesystem::path& path : ordered) {
        ModuleAst unit = loaded.at(path);
        add_import_aliases_for_unit(unit, loaded);
        loaded[path] = unit;
        merged.module_units.push_back(unit);
        append_module(merged, unit);
    }
    add_from_import_aliases(merged);
    return result;
}

ModuleAst load_source_tree(const LoadSourceTreeOptions& options) {
    return std::move(load_source_tree_impl(options, false).module);
}

LoadSourceTreeResult load_source_tree_recovering(const LoadSourceTreeOptions& options) {
    return load_source_tree_impl(options, true);
}

ModuleAst load_source_tree(const std::filesystem::path& entry,
                           const std::map<std::filesystem::path, std::string>& source_overrides) {
    return load_source_tree(
        {.entry = entry, .source_overrides = source_overrides, .module_roots = {}});
}

ModuleAst load_source_tree(const std::filesystem::path& entry, std::string_view entry_source) {
    return load_source_tree(entry, {{entry, std::string(entry_source)}});
}

ModuleAst load_source_tree(const std::filesystem::path& entry) {
    return load_source_tree(entry, read_text_file(entry));
}

std::vector<std::filesystem::path> source_tree_files(const std::filesystem::path& entry) {
    return source_tree_files(entry, {});
}

std::vector<std::filesystem::path>
source_tree_files(const std::filesystem::path& entry,
                  const std::map<std::string, std::filesystem::path>& module_roots) {
    std::map<std::string, std::filesystem::path> canonical_module_roots;
    for (const auto& [name, path] : with_standard_module_roots(module_roots)) {
        canonical_module_roots[name] = canonical_source_path(path);
    }
    std::vector<std::filesystem::path> stack;
    std::vector<std::filesystem::path> out;
    collect_files(entry, canonical_module_roots, stack, out);
    return out;
}

} // namespace dudu
