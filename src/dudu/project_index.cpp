#include "dudu/project_index.hpp"

#include "dudu/module_loader.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace dudu {
namespace {

std::filesystem::path canonical_key_path(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

std::string path_key(const std::filesystem::path& path) {
    return canonical_key_path(path).string();
}

bool has_dudu_module_imports(const ModuleAst& module) {
    return std::any_of(module.imports.begin(), module.imports.end(), [](const ImportDecl& import) {
        return import.kind == ImportKind::Module || import.kind == ImportKind::From;
    });
}

void apply_project_context(ModuleAst& module, const ProjectIndexOptions& options) {
    module.build_values = options.config.build_values;
    module.build_values["TARGET_KIND"] = '"' + options.config.target_kind + '"';
    module.build_values["TARGET_MODE"] = '"' + options.config.target_mode + '"';
    module.target_mode_explicit = options.config.target_mode_explicit;
    for (const auto& [name, value] : options.build_values) {
        module.build_values[name] = value;
    }
}

void apply_project_context_to_tree(ModuleAst& module, const ProjectIndexOptions& options) {
    apply_project_context(module, options);
    for (ModuleAst& unit : module.module_units) {
        apply_project_context(unit, options);
    }
}

void merge_native_headers_for_tree(ModuleAst& module, const ProjectIndexOptions& options) {
    const NativeHeaderOptions native_options{.config = options.config,
                                             .source_dir = options.source_dir};
    if (module.module_units.empty()) {
        merge_native_header_types(module, native_options);
        return;
    }
    if (options.include_native_headers_in_merged_module) {
        merge_native_header_types(module, native_options);
    }
    for (ModuleAst& unit : module.module_units) {
        merge_native_header_types(unit, native_options);
    }
}

void analyze_project_module(const ModuleAst& module, const ProjectIndexOptions& options) {
    if (!options.check_semantics) {
        return;
    }
    if (module.module_units.empty()) {
        reject_merged_output_module_conflicts(module);
        analyze_module(module, options.semantic_options);
        return;
    }
    analyze_module_tree(module, options.semantic_options);
}

ModuleAst load_project_module(const ProjectIndexOptions& options) {
    if (options.entry_path.empty()) {
        return parse_source(std::string(options.entry_source), options.entry_path);
    }
    if (options.allow_module_tree && options.force_module_tree) {
        return load_source_tree(options.entry_path, options.entry_source);
    }
    ModuleAst parsed = parse_source(std::string(options.entry_source), options.entry_path);
    if (options.allow_module_tree && has_dudu_module_imports(parsed)) {
        return load_source_tree(options.entry_path, options.entry_source);
    }
    return parsed;
}

void add_export_names(ProjectModuleSummary& summary, const ModuleAst& module) {
    for (const TypeAliasDecl& alias : module.aliases) {
        summary.exports.insert(alias.name);
    }
    for (const EnumDecl& en : module.enums) {
        summary.exports.insert(en.name);
        for (const EnumValueDecl& value : en.values) {
            summary.exports.insert(en.name + "." + value.name);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        summary.exports.insert(klass.name);
        for (const ConstDecl& constant : klass.constants) {
            summary.exports.insert(klass.name + "." + constant.name);
        }
        for (const FunctionDecl& method : klass.methods) {
            summary.exports.insert(klass.name + "." + method.name);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        summary.exports.insert(constant.name);
    }
    for (const FunctionDecl& fn : module.functions) {
        summary.exports.insert(fn.name);
    }
}

std::vector<const ModuleAst*> indexed_units(const ModuleAst& module) {
    std::vector<const ModuleAst*> out;
    if (module.module_units.empty()) {
        out.push_back(&module);
        return out;
    }
    for (const ModuleAst& unit : module.module_units) {
        out.push_back(&unit);
    }
    return out;
}

} // namespace

ProjectIndex ProjectIndex::load(ProjectIndexOptions options) {
    ProjectIndex index;
    index.module_ = load_project_module(options);
    apply_project_context_to_tree(index.module_, options);
    if (options.include_native_headers) {
        merge_native_headers_for_tree(index.module_, options);
    }
    analyze_project_module(index.module_, options);

    const std::vector<const ModuleAst*> units = indexed_units(index.module_);
    for (const ModuleAst* unit : units) {
        ProjectModuleSummary summary;
        summary.source_path = unit->source_path;
        summary.module_path = unit->module_path;
        summary.dependencies = unit->dependencies;
        add_export_names(summary, *unit);
        const size_t index_id = index.modules_.size();
        index.source_path_to_index_[path_key(unit->source_path)] = index_id;
        index.module_path_to_index_[unit->module_path] = index_id;
        index.modules_.push_back(std::move(summary));
    }

    for (ProjectModuleSummary& summary : index.modules_) {
        for (const ModuleDependency& dependency : summary.dependencies) {
            const auto found = index.module_path_to_index_.find(dependency.resolved_module_path);
            if (found == index.module_path_to_index_.end()) {
                continue;
            }
            index.modules_[found->second].reverse_dependencies.push_back(summary.module_path);
        }
    }
    return index;
}

const ProjectModuleSummary*
ProjectIndex::summary_for_path(const std::filesystem::path& path) const {
    const auto found = source_path_to_index_.find(path_key(path));
    return found == source_path_to_index_.end() ? nullptr : &modules_[found->second];
}

const ProjectModuleSummary*
ProjectIndex::summary_for_module(std::string_view module_path) const {
    const auto found = module_path_to_index_.find(std::string(module_path));
    return found == module_path_to_index_.end() ? nullptr : &modules_[found->second];
}

const ModuleAst* ProjectIndex::unit_for_path(const std::filesystem::path& path) const {
    const auto found = source_path_to_index_.find(path_key(path));
    if (found == source_path_to_index_.end()) {
        return nullptr;
    }
    if (module_.module_units.empty()) {
        return &module_;
    }
    return &module_.module_units[found->second];
}

const ModuleAst* ProjectIndex::unit_for_module(std::string_view module_path) const {
    const auto found = module_path_to_index_.find(std::string(module_path));
    if (found == module_path_to_index_.end()) {
        return nullptr;
    }
    if (module_.module_units.empty()) {
        return &module_;
    }
    return &module_.module_units[found->second];
}

const ModuleAst& ProjectIndex::visible_unit_for_path(const std::filesystem::path& path) const {
    if (const ModuleAst* unit = unit_for_path(path)) {
        return *unit;
    }
    if (!module_.module_units.empty()) {
        return module_.module_units.back();
    }
    return module_;
}

const ModuleAst* ProjectIndex::imported_unit(const ModuleAst& current,
                                             const ImportDecl& import) const {
    std::string resolved_module_path = import.module_path;
    std::filesystem::path resolved_source_path;
    for (const ModuleDependency& dependency : current.dependencies) {
        if (dependency.import_module_path == import.module_path) {
            resolved_module_path = dependency.resolved_module_path;
            resolved_source_path = dependency.source_path;
            break;
        }
    }
    if (!resolved_source_path.empty()) {
        if (const ModuleAst* unit = unit_for_path(resolved_source_path)) {
            return unit;
        }
    }
    return unit_for_module(resolved_module_path);
}

} // namespace dudu
