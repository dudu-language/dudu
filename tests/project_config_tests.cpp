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
    write_text(project / "dudu.toml",
               "name = \"pathprobe\"\n"
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

} // namespace

int main() {
    try {
        test_manifest_relative_paths(DUDU_REPO_ROOT);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
