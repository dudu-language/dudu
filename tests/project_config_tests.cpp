#include "dudu/cli_options.hpp"
#include "dudu/project_config.hpp"

#include <cassert>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("could not write " + path.string());
    }
    out << text;
}

void test_manifest_relative_paths(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-paths";
    std::filesystem::remove_all(project);
    write_text(project / "dudu.toml", "name = \"pathprobe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "build_dir = \"build\"\n"
                                      "\n"
                                      "[include]\n"
                                      "paths = [\"third_party/include\"]\n");
    write_text(project / "src" / "main.dd", "def main() -> i32:\n    return 0\n");

    const std::filesystem::path config_path = dudu::find_project_config(project / "src/main.dd");
    assert(config_path.lexically_normal() == (project / "dudu.toml").lexically_normal());

    const dudu::ProjectConfig config = dudu::parse_project_config(config_path);
    assert(config.project_dir.lexically_normal() ==
           std::filesystem::absolute(project).lexically_normal());
    assert(dudu::project_path(config, "third_party/include").lexically_normal() ==
           (project / "third_party/include").lexically_normal());
}

void test_build_backend_selection(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-backend";
    std::filesystem::remove_all(project);
    write_text(project / "dudu.toml", "name = \"backend_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[build]\n"
                                      "dir = \"build\"\n"
                                      "backend = \"cmake\"\n"
                                      "\n"
                                      "[cmake]\n"
                                      "source = \".\"\n"
                                      "target = \"backend_probe\"\n"
                                      "config = \"Debug\"\n"
                                      "generator = \"Ninja\"\n");
    write_text(project / "src" / "main.dd", "def main() -> i32:\n    return 0\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(project / "dudu.toml");
    assert(config.build_dir == "build");
    assert(config.build_backend == "cmake");
    assert(config.build_backend_explicit);
    assert(config.cmake_source == ".");
    assert(config.cmake_target == "backend_probe");
    assert(config.cmake_config == "Debug");
    assert(config.cmake_generator == "Ninja");

    write_text(project / "dudu.toml", "name = \"backend_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[build]\n"
                                      "backend = \"nonsense\"\n");
    bool rejected = false;
    try {
        (void)dudu::parse_project_config(project / "dudu.toml");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);

    write_text(project / "dudu.toml", "name = \"backend_probe\"\n"
                                      "entry = \"src/main.dd\"\n");
    const dudu::ProjectConfig implicit = dudu::parse_project_config(project / "dudu.toml");
    assert(implicit.build_backend == "direct");
    assert(!implicit.build_backend_explicit);
}

void test_project_driver_resolves_manifest_relative_entries(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-entry-resolution";
    const std::filesystem::path outside = root / "build" / "project-config-entry-outside";
    std::filesystem::remove_all(project);
    std::filesystem::remove_all(outside);
    std::filesystem::create_directories(outside);
    write_text(project / "dudu.toml", "name = \"entry_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[targets.tool]\n"
                                      "entry = \"tools/tool.dd\"\n");
    write_text(project / "src" / "main.dd", "def main() -> i32:\n    return 0\n");
    write_text(project / "tools" / "tool.dd", "def main() -> i32:\n    return 0\n");

    const std::filesystem::path original_cwd = std::filesystem::current_path();
    try {
        std::filesystem::current_path(outside);
        dudu::CliOptions by_directory;
        by_directory.project_driver = true;
        by_directory.build = true;
        by_directory.input = project;
        by_directory = dudu::resolve_project_input(std::move(by_directory));
        assert(by_directory.input.lexically_normal() ==
               (project / "src" / "main.dd").lexically_normal());

        std::filesystem::current_path(project);
        dudu::CliOptions by_target;
        by_target.project_driver = true;
        by_target.build = true;
        by_target.input = "tool";
        by_target = dudu::resolve_project_input(std::move(by_target));
        assert(by_target.target_name == "tool");
        assert(by_target.input.lexically_normal() ==
               (project / "tools" / "tool.dd").lexically_normal());

        dudu::CliOptions by_default;
        by_default.project_driver = true;
        by_default.run = true;
        by_default = dudu::resolve_project_input(std::move(by_default));
        assert(by_default.input.lexically_normal() ==
               (project / "src" / "main.dd").lexically_normal());
    } catch (...) {
        std::filesystem::current_path(original_cwd);
        throw;
    }
    std::filesystem::current_path(original_cwd);
}

} // namespace

int main() {
    try {
        test_manifest_relative_paths(DUDU_REPO_ROOT);
        test_build_backend_selection(DUDU_REPO_ROOT);
        test_project_driver_resolves_manifest_relative_entries(DUDU_REPO_ROOT);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
