#include "dudu/module_loader.hpp"

#include "dudu/parser.hpp"
#include "dudu/source.hpp"

#include <fstream>
#include <set>

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
    target.enums.insert(target.enums.end(), source.enums.begin(), source.enums.end());
    target.classes.insert(target.classes.end(), source.classes.begin(), source.classes.end());
    target.constants.insert(target.constants.end(), source.constants.begin(),
                            source.constants.end());
    target.functions.insert(target.functions.end(), source.functions.begin(),
                            source.functions.end());
    target.static_asserts.insert(target.static_asserts.end(), source.static_asserts.begin(),
                                 source.static_asserts.end());
}

ModuleAst load_one(const std::filesystem::path& path, std::set<std::filesystem::path>& loading,
                   std::set<std::filesystem::path>& loaded) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (loaded.contains(canonical)) {
        return {};
    }
    if (loading.contains(canonical)) {
        throw CompileError({.file = path, .line = 1, .column = 1}, "cyclic module import");
    }
    loading.insert(canonical);

    ModuleAst parsed = parse_source(read_text_file(path), path);
    ModuleAst merged;
    for (const ImportDecl& import : parsed.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const std::filesystem::path dependency =
            module_path_to_file(path.parent_path(), import.module_path);
        append_module(merged, load_one(dependency, loading, loaded));
    }
    append_module(merged, std::move(parsed));

    loading.erase(canonical);
    loaded.insert(canonical);
    return merged;
}

} // namespace

ModuleAst load_source_tree(const std::filesystem::path& entry) {
    std::set<std::filesystem::path> loading;
    std::set<std::filesystem::path> loaded;
    return load_one(entry, loading, loaded);
}

} // namespace dudu
