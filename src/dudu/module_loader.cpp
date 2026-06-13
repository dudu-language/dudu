#include "dudu/module_loader.hpp"

#include "dudu/parser.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
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

void append_module(ModuleAst& target, ModuleAst source) {
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

ModuleAst load_one(const std::filesystem::path& path, std::set<std::filesystem::path>& loading,
                   std::map<std::filesystem::path, ModuleAst>& loaded) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (loaded.contains(canonical)) {
        return loaded.at(canonical);
    }
    if (loading.contains(canonical)) {
        throw CompileError({.file = path, .line = 1, .column = 1}, "cyclic module import");
    }
    loading.insert(canonical);

    ModuleAst parsed = parse_source(read_text_file(path), path);
    ModuleAst merged;
    std::set<std::filesystem::path> appended_dependencies;
    for (const ImportDecl& import : parsed.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const std::filesystem::path dependency =
            module_path_to_file(path.parent_path(), import.module_path);
        const std::filesystem::path canonical_dependency =
            std::filesystem::weakly_canonical(dependency);
        ModuleAst dependency_module = load_one(dependency, loading, loaded);
        if (import.kind == ImportKind::From &&
            !has_module_symbol(dependency_module, import.imported_name)) {
            throw CompileError(import.location, "module '" + import.module_path +
                                                    "' has no symbol '" + import.imported_name +
                                                    "'");
        }
        if (appended_dependencies.insert(canonical_dependency).second) {
            append_module(merged, std::move(dependency_module));
        }
    }
    append_module(merged, std::move(parsed));

    loading.erase(canonical);
    loaded[canonical] = merged;
    return merged;
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
    std::set<std::filesystem::path> loading;
    std::map<std::filesystem::path, ModuleAst> loaded;
    return load_one(entry, loading, loaded);
}

std::vector<std::filesystem::path> source_tree_files(const std::filesystem::path& entry) {
    std::set<std::filesystem::path> loading;
    std::vector<std::filesystem::path> out;
    collect_files(entry, loading, out);
    return out;
}

} // namespace dudu
