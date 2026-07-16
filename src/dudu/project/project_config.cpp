#include "dudu/project/project_config.hpp"

namespace dudu {
namespace {

void append_all(std::vector<std::string>& target, const std::vector<std::string>& source) {
    target.insert(target.end(), source.begin(), source.end());
}

} // namespace

ProjectConfig apply_project_target(ProjectConfig config, const std::string& target_name) {
    const auto found = config.targets.find(target_name);
    if (found == config.targets.end()) {
        return config;
    }
    const ProjectTarget& target = found->second;
    config.name = target_name;
    if (!target.main.empty()) {
        config.main = target.main;
    }
    if (!target.target_kind.empty()) {
        config.target_kind = target.target_kind;
    }
    if (!target.target_mode.empty()) {
        config.target_mode = target.target_mode;
        config.target_mode_explicit = target.target_mode_explicit;
    }
    append_all(config.c_sources, target.c_sources);
    append_all(config.cpp_sources, target.cpp_sources);
    append_all(config.defines, target.defines);
    append_all(config.flags, target.flags);
    append_all(config.include_dirs, target.include_dirs);
    append_all(config.lib_dirs, target.lib_dirs);
    append_all(config.libs, target.libs);
    append_all(config.link_flags, target.link_flags);
    append_all(config.pkg_config_packages, target.pkg_config_packages);
    append_all(config.pkg_config_paths, target.pkg_config_paths);
    return config;
}

std::filesystem::path find_project_config(const std::filesystem::path& input) {
    std::filesystem::path dir;
    if (input.empty()) {
        dir = std::filesystem::current_path();
    } else if (std::filesystem::is_directory(input)) {
        dir = std::filesystem::absolute(input);
    } else if (input.has_parent_path()) {
        dir = std::filesystem::absolute(input.parent_path());
    } else {
        dir = std::filesystem::current_path();
    }
    while (true) {
        const std::filesystem::path candidate = dir / "dudu.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir == dir.parent_path()) {
            break;
        }
        dir = dir.parent_path();
    }
    return "dudu.toml";
}

std::filesystem::path project_path(const ProjectConfig& config, const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }
    const std::filesystem::path root =
        config.project_dir.empty() ? std::filesystem::current_path() : config.project_dir;
    return (root / path).lexically_normal();
}

} // namespace dudu
