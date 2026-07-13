#include "dudu/project/project_index.hpp"

#include "dudu/core/file_io.hpp"
#include "dudu/macro/macro_expansion.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_loader.hpp"
#include "dudu/project/module_names.hpp"
#include "dudu/project/project_dependencies.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
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

std::optional<std::filesystem::file_time_type> file_mtime(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(path, error);
    return error ? std::nullopt : std::optional<std::filesystem::file_time_type>{mtime};
}

std::string mtime_stamp(std::optional<std::filesystem::file_time_type> mtime) {
    if (!mtime.has_value()) {
        return {};
    }
    return file_time_stamp(*mtime);
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

struct ProjectModuleLoad {
    ModuleAst module;
    std::vector<ParseDiagnostic> diagnostics;
};

ProjectModuleLoad load_project_module(const ProjectIndexOptions& options) {
    const auto stamp_single_module = [&](ModuleAst module) {
        if (!options.entry_path.empty() && module.source_path.empty()) {
            module.source_path = canonical_key_path(options.entry_path);
        }
        if (!module.source_path.empty() && module.module_path.empty()) {
            const std::filesystem::path root =
                module.source_path.has_parent_path() ? module.source_path.parent_path() : ".";
            module.module_path = module_name_from_file(root, module.source_path);
        }
        return module;
    };
    if (options.entry_path.empty()) {
        if (options.recover_syntax) {
            ParseResult recovered =
                parse_source_recovering(std::string(options.entry_source), options.entry_path);
            return {.module = std::move(recovered.module),
                    .diagnostics = std::move(recovered.diagnostics)};
        }
        return {.module = parse_source(std::string(options.entry_source), options.entry_path),
                .diagnostics = {}};
    }
    std::map<std::filesystem::path, std::string> source_overrides = options.source_overrides;
    if (!options.entry_source.empty() && !source_overrides.contains(options.entry_path)) {
        source_overrides[options.entry_path] = std::string(options.entry_source);
    }
    if (options.allow_module_tree && options.force_module_tree) {
        const LoadSourceTreeOptions load_options{.entry = options.entry_path,
                                                 .source_overrides = source_overrides,
                                                 .module_roots =
                                                     dependency_module_roots(options.config)};
        if (options.recover_syntax) {
            LoadSourceTreeResult recovered = load_source_tree_recovering(load_options);
            return {.module = std::move(recovered.module),
                    .diagnostics = std::move(recovered.diagnostics)};
        }
        return {.module = load_source_tree(load_options), .diagnostics = {}};
    }
    ParseResult recovered;
    ModuleAst parsed;
    if (options.recover_syntax) {
        recovered = parse_source_recovering(std::string(options.entry_source), options.entry_path);
        parsed = stamp_single_module(std::move(recovered.module));
    } else {
        parsed = stamp_single_module(
            parse_source(std::string(options.entry_source), options.entry_path));
    }
    if (options.allow_module_tree && has_dudu_module_imports(parsed)) {
        const LoadSourceTreeOptions load_options{.entry = options.entry_path,
                                                 .source_overrides = source_overrides,
                                                 .module_roots =
                                                     dependency_module_roots(options.config)};
        if (options.recover_syntax) {
            LoadSourceTreeResult tree = load_source_tree_recovering(load_options);
            return {.module = std::move(tree.module), .diagnostics = std::move(tree.diagnostics)};
        }
        return {.module = load_source_tree(load_options), .diagnostics = {}};
    }
    return {.module = std::move(parsed), .diagnostics = std::move(recovered.diagnostics)};
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

void add_native_identity(std::map<std::string, std::set<std::string>>& out, const std::string& name,
                         const NativeSymbolId& identity) {
    const std::string key = native_symbol_identity_key(identity);
    if (name.empty() || key.empty()) {
        return;
    }
    out[name].insert(key);
}

std::map<std::string, std::set<std::string>>
native_identity_index_for_module(const ModuleAst& module) {
    std::map<std::string, std::set<std::string>> out;
    for (const NativeNamespaceDecl& ns : module.native_namespaces) {
        add_native_identity(out, ns.name, ns.identity);
    }
    for (const NativeTypeDecl& type : module.native_types) {
        add_native_identity(out, type.name, type.identity);
    }
    for (const NativeValueDecl& value : module.native_values) {
        add_native_identity(out, value.name, value.identity);
    }
    for (const NativeMacroDecl& macro : module.native_macros) {
        add_native_identity(out, macro.name, macro.identity);
    }
    for (const NativeFunctionDecl& fn : module.native_functions) {
        add_native_identity(out, fn.name, fn.identity);
    }
    for (const ClassDecl& klass : module.native_classes) {
        add_native_identity(out, klass.name, klass.identity);
        for (const FieldDecl& field : klass.fields) {
            const std::string key = native_class_member_symbol_identity_key(klass, field.name);
            if (!key.empty()) {
                out[klass.name + "." + field.name].insert(key);
            }
        }
        for (const ConstDecl& constant : klass.constants) {
            const std::string key = native_class_member_symbol_identity_key(klass, constant.name);
            if (!key.empty()) {
                out[klass.name + "." + constant.name].insert(key);
            }
        }
        for (const ConstDecl& field : klass.static_fields) {
            const std::string key = native_class_member_symbol_identity_key(klass, field.name);
            if (!key.empty()) {
                out[klass.name + "." + field.name].insert(key);
            }
        }
        for (const FunctionDecl& method : klass.methods) {
            add_native_identity(out, klass.name + "." + method.name, method.native_identity);
        }
    }
    return out;
}

std::map<std::string, std::set<std::string>> reverse_native_identity_index(
    const std::map<std::string, std::set<std::string>>& by_name) {
    std::map<std::string, std::set<std::string>> out;
    for (const auto& [name, identities] : by_name) {
        for (const std::string& identity : identities) {
            out[identity].insert(name);
        }
    }
    return out;
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

struct SourceStamp {
    std::string module_path;
    std::string mtime;
    std::string source_key;
};

std::vector<SourceStamp> parse_source_stamp_file(const std::filesystem::path& path) {
    std::optional<std::string> text = try_read_text_file(path);
    if (!text.has_value()) {
        return {};
    }
    std::vector<SourceStamp> out;
    std::istringstream lines(*text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) {
            continue;
        }
        const size_t first = line.find('\t');
        const size_t second =
            first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        out.push_back({.module_path = line.substr(0, first),
                       .mtime = line.substr(first + 1, second - first - 1),
                       .source_key = line.substr(second + 1)});
    }
    return out;
}

} // namespace

ProjectIndex ProjectIndex::load(ProjectIndexOptions options) {
    ProjectIndex index;
    ProjectModuleLoad loaded = load_project_module(options);
    index.module_ = std::move(loaded.module);
    index.parse_diagnostics_ = std::move(loaded.diagnostics);
    apply_project_context_to_tree(index.module_, options);
    if (options.include_native_headers) {
        merge_native_headers_for_tree(index.module_, options);
    }
    if (options.expand_macros) {
        index.macro_report_ = macro::expand_module_macros(
            index.module_, {.project = options.config,
                            .cache_dir = {},
                            .request_timeout = std::chrono::milliseconds(5000)});
    }
    analyze_project_module(index.module_, options);

    const std::vector<const ModuleAst*> units = indexed_units(index.module_);
    for (const ModuleAst* unit : units) {
        ProjectModuleSummary summary;
        summary.source_path = unit->source_path;
        summary.module_path = unit->module_path;
        summary.source_mtime = file_mtime(unit->source_path);
        summary.dependencies = unit->dependencies;
        add_export_names(summary, *unit);
        const size_t index_id = index.modules_.size();
        index.source_path_to_index_[path_key(unit->source_path)] = index_id;
        index.module_path_to_index_[unit->module_path] = index_id;
        index.modules_.push_back(std::move(summary));
        std::map<std::string, std::set<std::string>> native_identities =
            native_identity_index_for_module(*unit);
        index.native_queries_by_identity_.push_back(
            reverse_native_identity_index(native_identities));
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

std::vector<std::filesystem::path> ProjectIndex::source_files() const {
    std::vector<std::filesystem::path> out;
    out.reserve(modules_.size());
    for (const ProjectModuleSummary& summary : modules_) {
        if (!summary.source_path.empty()) {
            out.push_back(summary.source_path);
        }
    }
    return out;
}

const ProjectModuleSummary*
ProjectIndex::summary_for_path(const std::filesystem::path& path) const {
    const auto found = source_path_to_index_.find(path_key(path));
    return found == source_path_to_index_.end() ? nullptr : &modules_[found->second];
}

const ProjectModuleSummary* ProjectIndex::summary_for_module(std::string_view module_path) const {
    const auto found = module_path_to_index_.find(std::string(module_path));
    return found == module_path_to_index_.end() ? nullptr : &modules_[found->second];
}

std::vector<std::string>
ProjectIndex::affected_modules_for_sources(const std::vector<std::filesystem::path>& paths) const {
    std::set<std::string> affected;
    std::vector<std::string> stack;
    for (const std::filesystem::path& path : paths) {
        const ProjectModuleSummary* summary = summary_for_path(path);
        if (summary == nullptr) {
            continue;
        }
        if (affected.insert(summary->module_path).second) {
            stack.push_back(summary->module_path);
        }
    }
    while (!stack.empty()) {
        const std::string module_path = std::move(stack.back());
        stack.pop_back();
        const ProjectModuleSummary* summary = summary_for_module(module_path);
        if (summary == nullptr) {
            continue;
        }
        for (const std::string& dependent : summary->reverse_dependencies) {
            if (affected.insert(dependent).second) {
                stack.push_back(dependent);
            }
        }
    }

    std::vector<std::string> out;
    out.reserve(affected.size());
    for (const ProjectModuleSummary& summary : modules_) {
        if (affected.contains(summary.module_path)) {
            out.push_back(summary.module_path);
        }
    }
    return out;
}

std::vector<std::filesystem::path>
ProjectIndex::changed_sources_since_stamp_file(const std::filesystem::path& path) const {
    const std::vector<SourceStamp> previous_stamps = parse_source_stamp_file(path);
    std::map<std::string, SourceStamp> previous_by_module;
    for (const SourceStamp& stamp : previous_stamps) {
        previous_by_module[stamp.module_path] = stamp;
    }

    std::vector<std::filesystem::path> changed;
    for (const ProjectModuleSummary& summary : modules_) {
        if (summary.source_path.empty()) {
            continue;
        }
        const auto previous = previous_by_module.find(summary.module_path);
        if (previous == previous_by_module.end() ||
            previous->second.source_key != path_key(summary.source_path) ||
            previous->second.mtime != mtime_stamp(summary.source_mtime)) {
            changed.push_back(summary.source_path);
        }
    }
    return changed;
}

void ProjectIndex::write_source_stamp_file(const std::filesystem::path& path) const {
    if (path.empty()) {
        return;
    }
    std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("could not open source stamp file " + path.string());
    }
    for (const ProjectModuleSummary& summary : modules_) {
        if (summary.source_path.empty()) {
            continue;
        }
        out << summary.module_path << '\t' << mtime_stamp(summary.source_mtime) << '\t'
            << path_key(summary.source_path) << '\n';
    }
}

bool ProjectIndex::source_stamps_current() const {
    for (const ProjectModuleSummary& summary : modules_) {
        if (summary.source_path.empty() || !summary.source_mtime.has_value()) {
            continue;
        }
        if (file_mtime(summary.source_path) != summary.source_mtime) {
            return false;
        }
    }
    return true;
}

bool source_stamp_file_current(const std::filesystem::path& path) {
    const std::vector<SourceStamp> stamps = parse_source_stamp_file(path);
    if (stamps.empty()) {
        return false;
    }
    for (const SourceStamp& stamp : stamps) {
        if (stamp.source_key.empty() || stamp.mtime.empty()) {
            return false;
        }
        if (mtime_stamp(file_mtime(std::filesystem::path(stamp.source_key))) != stamp.mtime) {
            return false;
        }
    }
    return true;
}

bool source_stamp_file_current_for_entry(const std::filesystem::path& path,
                                         const std::filesystem::path& entry_path) {
    const std::vector<SourceStamp> stamps = parse_source_stamp_file(path);
    if (stamps.empty()) {
        return false;
    }
    const std::string entry_key = path_key(entry_path);
    bool found_entry = false;
    for (const SourceStamp& stamp : stamps) {
        if (stamp.source_key.empty() || stamp.mtime.empty()) {
            return false;
        }
        if (stamp.source_key == entry_key) {
            found_entry = true;
        }
        if (mtime_stamp(file_mtime(std::filesystem::path(stamp.source_key))) != stamp.mtime) {
            return false;
        }
    }
    return found_entry;
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

std::set<std::string>
ProjectIndex::native_queries_for_identity(const std::filesystem::path& path,
                                          std::string_view identity) const {
    if (identity.empty()) {
        return {};
    }
    const auto found = source_path_to_index_.find(path_key(path));
    if (found == source_path_to_index_.end() ||
        found->second >= native_queries_by_identity_.size()) {
        return {};
    }
    const auto& by_identity = native_queries_by_identity_[found->second];
    const auto queries = by_identity.find(std::string(identity));
    return queries == by_identity.end() ? std::set<std::string>{} : queries->second;
}

} // namespace dudu
