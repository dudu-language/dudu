#include "dudu/module_loader.hpp"

#include "dudu/ast_parse_utils.hpp"
#include "dudu/module_import_aliases.hpp"
#include "dudu/parser.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw CompileError({.file = path, .line = 1, .column = 1}, "could not open module");
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::filesystem::path module_path_to_file(const std::filesystem::path& base,
                                          const std::string& module_path) {
    std::filesystem::path out = base;
    size_t start = 0;
    while (start < module_path.size()) {
        const size_t dot = module_path.find('.', start);
        const size_t end = dot == std::string::npos ? module_path.size() : dot;
        out /= module_path.substr(start, end - start);
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    out += ".dd";
    return out;
}

std::string module_name_from_file(const std::filesystem::path& root,
                                  const std::filesystem::path& file) {
    std::filesystem::path relative = std::filesystem::relative(file, root);
    relative.replace_extension();
    std::ostringstream out;
    for (const std::filesystem::path& part : relative) {
        if (!out.str().empty()) {
            out << '.';
        }
        out << part.string();
    }
    return out.str();
}

std::string cpp_name_piece(const std::string& text, bool pascal) {
    std::string out;
    bool upper_next = pascal;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) {
            upper_next = pascal;
            if (!pascal && !out.empty() && out.back() != '_') {
                out.push_back('_');
            }
            continue;
        }
        if (pascal && upper_next) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            upper_next = false;
        } else {
            out.push_back(c);
        }
    }
    if (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? (pascal ? "Main" : "main") : out;
}

std::string module_cpp_prefix(const std::string& module_path, bool pascal) {
    if (module_path.empty()) {
        return pascal ? "DuduMain" : "dudu_main";
    }
    std::ostringstream out;
    if (pascal) {
        out << "Dudu";
    } else {
        out << "dudu";
    }
    size_t start = 0;
    while (start < module_path.size()) {
        const size_t dot = module_path.find('.', start);
        const size_t end = dot == std::string::npos ? module_path.size() : dot;
        const std::string piece = module_path.substr(start, end - start);
        if (pascal) {
            out << cpp_name_piece(piece, true);
        } else {
            out << '_' << cpp_name_piece(piece, false);
        }
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return out.str();
}

std::string generated_type_name(const std::string& module_path, const std::string& name) {
    return module_cpp_prefix(module_path, true) + cpp_name_piece(name, true);
}

std::string generated_value_name(const std::string& module_path, const std::string& name) {
    return module_cpp_prefix(module_path, false) + "_" + cpp_name_piece(name, false);
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

bool has_merged_symbol(const ModuleAst& module, const std::string& name) {
    return has_module_symbol(module, name);
}

void add_import_aliases_for_unit(ModuleAst& module, const std::filesystem::path& module_path,
                                 const std::map<std::filesystem::path, ModuleAst>& loaded) {
    for (const ImportDecl& import : module.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const std::filesystem::path dependency_path = std::filesystem::weakly_canonical(
            module_path_to_file(module_path.parent_path(), import.module_path));
        const auto dependency = loaded.find(dependency_path);
        if (dependency == loaded.end()) {
            continue;
        }
        if (import.kind == ImportKind::Module) {
            add_qualified_module_symbols(module, dependency->second, import);
        } else {
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
        if (has_merged_symbol(module, import.alias) || pending_aliases.contains(import.alias)) {
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
                alias.type = import.imported_name;
                alias.type_ref = parse_type_text(import.imported_name, import.location);
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
                alias.type = import.imported_name;
                alias.type_ref = parse_type_text(import.imported_name, import.location);
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
                break;
            }
        }
        if (!added) {
            throw CompileError(import.location, "module '" + import.module_path +
                                                    "' has no symbol '" + import.imported_name +
                                                    "'");
        }
    }

    module.aliases.insert(module.aliases.end(), type_aliases.begin(), type_aliases.end());
    module.constants.insert(module.constants.end(), const_aliases.begin(), const_aliases.end());
    module.functions.insert(module.functions.end(), function_aliases.begin(),
                            function_aliases.end());
}

const ModuleAst& load_one(const std::filesystem::path& path, const std::filesystem::path& root,
                          std::set<std::filesystem::path>& loading,
                          std::map<std::filesystem::path, ModuleAst>& loaded,
                          std::vector<std::filesystem::path>& ordered) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (loaded.contains(canonical)) {
        return loaded.at(canonical);
    }
    if (loading.contains(canonical)) {
        throw CompileError({.file = path, .line = 1, .column = 1}, "cyclic module import");
    }
    loading.insert(canonical);

    ModuleAst parsed = parse_source(read_text_file(path), path);
    stamp_module_origin(parsed, canonical, module_name_from_file(root, canonical));
    for (const ImportDecl& import : parsed.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const std::filesystem::path dependency =
            module_path_to_file(path.parent_path(), import.module_path);
        const ModuleAst& dependency_module = load_one(dependency, root, loading, loaded, ordered);
        if (import.kind == ImportKind::From &&
            !has_module_symbol(dependency_module, import.imported_name)) {
            throw CompileError(import.location, "module '" + import.module_path +
                                                    "' has no symbol '" + import.imported_name +
                                                    "'");
        }
    }

    loading.erase(canonical);
    loaded.emplace(canonical, std::move(parsed));
    ordered.push_back(canonical);
    return loaded.at(canonical);
}

void collect_files(const std::filesystem::path& path, std::set<std::filesystem::path>& loading,
                   std::vector<std::filesystem::path>& out) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (loading.contains(canonical)) {
        throw CompileError({.file = path, .line = 1, .column = 1}, "cyclic module import");
    }
    if (std::find(out.begin(), out.end(), canonical) != out.end()) {
        return;
    }
    loading.insert(canonical);
    out.push_back(canonical);
    const ModuleAst parsed = parse_source(read_text_file(path), path);
    for (const ImportDecl& import : parsed.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        collect_files(module_path_to_file(path.parent_path(), import.module_path), loading, out);
    }
    loading.erase(canonical);
}

} // namespace

ModuleAst load_source_tree(const std::filesystem::path& entry) {
    const std::filesystem::path canonical_entry = std::filesystem::weakly_canonical(entry);
    const std::filesystem::path root = canonical_entry.parent_path();
    std::set<std::filesystem::path> loading;
    std::map<std::filesystem::path, ModuleAst> loaded;
    std::vector<std::filesystem::path> ordered;
    (void)load_one(canonical_entry, root, loading, loaded, ordered);

    ModuleAst merged;
    merged.source_path = canonical_entry;
    merged.module_path = module_name_from_file(root, canonical_entry);
    for (const std::filesystem::path& path : ordered) {
        ModuleAst unit = loaded.at(path);
        add_import_aliases_for_unit(unit, path, loaded);
        loaded[path] = unit;
        merged.module_units.push_back(unit);
        append_module(merged, unit);
    }
    add_from_import_aliases(merged);
    return merged;
}

std::vector<std::filesystem::path> source_tree_files(const std::filesystem::path& entry) {
    std::set<std::filesystem::path> loading;
    std::vector<std::filesystem::path> out;
    collect_files(entry, loading, out);
    return out;
}

} // namespace dudu
