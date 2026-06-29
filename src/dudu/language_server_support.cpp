#include "dudu/language_server_support.hpp"

#include "dudu/module_loader.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <string>

namespace dudu {
namespace {

struct ModuleCacheKey {
    std::string path;
    size_t text_hash = 0;
    bool include_native_headers = false;

    friend bool operator<(const ModuleCacheKey& lhs, const ModuleCacheKey& rhs) {
        if (lhs.path != rhs.path) {
            return lhs.path < rhs.path;
        }
        if (lhs.text_hash != rhs.text_hash) {
            return lhs.text_hash < rhs.text_hash;
        }
        return lhs.include_native_headers < rhs.include_native_headers;
    }
};

std::map<ModuleCacheKey, ModuleAst> module_cache;

} // namespace

std::string file_uri_to_path(std::string uri) {
    constexpr std::string_view prefix = "file://";
    if (uri.rfind(prefix, 0) == 0) {
        uri.erase(0, prefix.size());
    }
    std::string out;
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const std::string hex = uri.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(uri[i]);
        }
    }
    return out;
}

std::filesystem::path project_config_path(const std::filesystem::path& file) {
    std::filesystem::path dir = file.has_parent_path() ? file.parent_path() : ".";
    while (true) {
        const std::filesystem::path candidate = dir / "dudu.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir == dir.parent_path()) {
            return {};
        }
        dir = dir.parent_path();
    }
}

ProjectConfig config_for_file(const std::filesystem::path& file) {
    const std::filesystem::path config = project_config_path(file);
    if (config.empty()) {
        return {};
    }
    ProjectConfig parsed = parse_project_config(config);
    auto absolutize = [&](std::vector<std::string>& paths) {
        for (std::string& path_text : paths) {
            path_text = project_path(parsed, path_text).string();
        }
    };
    absolutize(parsed.include_dirs);
    absolutize(parsed.lib_dirs);
    return parsed;
}

bool module_has_dudu_imports(const ModuleAst& module) {
    return std::any_of(module.imports.begin(), module.imports.end(), [](const ImportDecl& import) {
        return import.kind == ImportKind::Module || import.kind == ImportKind::From;
    });
}

void apply_project_context(ModuleAst& module, const ProjectConfig& config) {
    module.build_values = config.build_values;
    module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
    module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
    module.target_mode_explicit = config.target_mode_explicit;
}

ModuleAst module_for_document(const Document& doc, bool include_native_headers) {
    const ModuleCacheKey key{.path = doc.path.lexically_normal().string(),
                             .text_hash = std::hash<std::string>{}(doc.text),
                             .include_native_headers = include_native_headers};
    if (const auto found = module_cache.find(key); found != module_cache.end()) {
        return found->second;
    }
    const ProjectConfig config = config_for_file(doc.path);
    ModuleAst parsed = parse_source(doc.text, doc.path);
    const bool saved_tree =
        std::filesystem::exists(doc.path) && source_tree_files(doc.path).size() > 1;
    const bool project_tree = saved_tree || module_has_dudu_imports(parsed);
    ModuleAst module = project_tree && std::filesystem::exists(doc.path)
                           ? load_source_tree(doc.path, doc.text)
                           : std::move(parsed);
    apply_project_context(module, config);
    if (include_native_headers) {
        const NativeHeaderOptions native_options{.config = config,
                                                 .source_dir = doc.path.parent_path()};
        merge_native_header_types(module, native_options);
        for (ModuleAst& unit : module.module_units) {
            apply_project_context(unit, config);
            merge_native_header_types(unit, native_options);
        }
    }
    module_cache.emplace(key, module);
    return module;
}

void clear_language_server_module_cache() {
    module_cache.clear();
}

const ModuleAst& visible_module_unit(const ModuleAst& module, const std::filesystem::path& path) {
    if (module.module_units.empty()) {
        return module;
    }
    const std::filesystem::path target =
        std::filesystem::exists(path) ? std::filesystem::weakly_canonical(path) : path;
    for (const ModuleAst& unit : module.module_units) {
        const std::filesystem::path unit_path =
            std::filesystem::exists(unit.source_path)
                ? std::filesystem::weakly_canonical(unit.source_path)
                : unit.source_path;
        if (unit_path == target) {
            return unit;
        }
    }
    return module.module_units.back();
}

const ModuleAst* imported_module_unit(const ModuleAst& module, const ModuleAst& current,
                                      const ImportDecl& import) {
    std::string resolved_module_path = import.module_path;
    std::filesystem::path resolved_source_path;
    for (const ModuleDependency& dependency : current.dependencies) {
        if (dependency.import_module_path == import.module_path) {
            resolved_module_path = dependency.resolved_module_path;
            resolved_source_path = dependency.source_path;
            break;
        }
    }
    for (const ModuleAst& unit : module.module_units) {
        if (!resolved_source_path.empty() && unit.source_path == resolved_source_path) {
            return &unit;
        }
        if (unit.module_path == resolved_module_path) {
            return &unit;
        }
    }
    if (module.module_path == resolved_module_path) {
        return &module;
    }
    return nullptr;
}

int leading_spaces(const std::string& line) {
    int out = 0;
    while (out < static_cast<int>(line.size()) && line[static_cast<size_t>(out)] == ' ') {
        ++out;
    }
    return out;
}

int document_line_count(const std::string& text) {
    return static_cast<int>(std::count(text.begin(), text.end(), '\n')) +
           (text.empty() || text.back() == '\n' ? 0 : 1);
}

std::vector<std::string> document_lines(const std::string& text) {
    std::istringstream in(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace dudu
