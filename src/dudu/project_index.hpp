#pragma once

#include "dudu/ast.hpp"
#include "dudu/project_config.hpp"
#include "dudu/sema.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

struct ProjectIndexOptions {
    std::filesystem::path entry_path;
    std::string_view entry_source;
    ProjectConfig config;
    std::filesystem::path source_dir;
    std::map<std::string, std::string> build_values;
    bool force_module_tree = false;
    bool allow_module_tree = true;
    bool include_native_headers = true;
    bool include_native_headers_in_merged_module = false;
    bool check_semantics = true;
    SemanticOptions semantic_options{};
};

struct ProjectModuleSummary {
    std::filesystem::path source_path;
    std::string module_path;
    std::optional<std::filesystem::file_time_type> source_mtime;
    std::set<std::string> exports;
    std::vector<ModuleDependency> dependencies;
    std::vector<std::string> reverse_dependencies;
};

class ProjectIndex {
  public:
    static ProjectIndex load(ProjectIndexOptions options);

    const ModuleAst& merged_module() const { return module_; }
    const std::vector<ProjectModuleSummary>& modules() const { return modules_; }
    const ProjectModuleSummary* summary_for_path(const std::filesystem::path& path) const;
    const ProjectModuleSummary* summary_for_module(std::string_view module_path) const;
    bool source_stamps_current() const;
    const ModuleAst* unit_for_path(const std::filesystem::path& path) const;
    const ModuleAst* unit_for_module(std::string_view module_path) const;
    const ModuleAst& visible_unit_for_path(const std::filesystem::path& path) const;
    const ModuleAst* imported_unit(const ModuleAst& current, const ImportDecl& import) const;

  private:
    ModuleAst module_;
    std::vector<ProjectModuleSummary> modules_;
    std::map<std::string, size_t> source_path_to_index_;
    std::map<std::string, size_t> module_path_to_index_;
};

} // namespace dudu
