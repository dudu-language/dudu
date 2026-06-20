#include "dudu/project_driver.hpp"

#include "dudu/project_config.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace dudu {
namespace {

bool project_step_timings = false;
std::chrono::steady_clock::time_point project_step_start = std::chrono::steady_clock::now();

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void write_new_file(const std::filesystem::path& path, const std::string& text) {
    if (std::filesystem::exists(path)) {
        fail("refusing to overwrite " + path.string());
    }
    std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
    std::ofstream out(path);
    if (!out) {
        fail("could not write " + path.string());
    }
    out << text;
}

std::string shell_quote_path(const std::filesystem::path& path) {
    std::string out = "'";
    for (const char c : path.string()) {
        out += c == '\'' ? "'\\''" : std::string(1, c);
    }
    out += "'";
    return out;
}

std::string project_name(const std::filesystem::path& dir) {
    const std::filesystem::path name = dir.filename();
    return name.empty() || name == "." ? "dudu_app" : name.string();
}

bool has_enclosing_git_repo(std::filesystem::path dir) {
    dir = std::filesystem::absolute(std::move(dir));
    while (true) {
        if (std::filesystem::exists(dir / ".git")) {
            return true;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent.empty() || parent == dir) {
            return false;
        }
        dir = parent;
    }
}

bool init_git_repo(const std::filesystem::path& dir) {
    const std::string command = "git init -q " + shell_quote_path(dir);
    return std::system(command.c_str()) == 0;
}

std::string elapsed_prefix() {
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = now - project_step_start;
    std::ostringstream out;
    out << "[+" << std::fixed << std::setprecision(3) << elapsed.count() << "s] ";
    return out.str();
}

} // namespace

std::filesystem::path clean_project(const std::filesystem::path& dir) {
    const std::filesystem::path root = dir.empty() ? "." : dir;
    const ProjectConfig config = parse_project_config(root / "dudu.toml");
    const std::filesystem::path build_dir = config.build_dir.empty() ? "build" : config.build_dir;
    const std::filesystem::path target = build_dir.is_absolute() ? build_dir : root / build_dir;
    std::filesystem::remove_all(target);
    return target;
}

void init_project(const std::filesystem::path& dir) {
    const std::string name = project_name(dir);
    std::filesystem::create_directories(dir);
    const bool write_gitignore = !has_enclosing_git_repo(dir);
    if (write_gitignore) {
        (void)init_git_repo(dir);
    }
    write_new_file(dir / "dudu.toml",
                   "name = \"" + name + "\"\n"
                   "entry = \"src/main.dd\"\n"
                   "\n"
                   "[cxx]\n"
                   "standard = \"c++20\"\n"
                   "\n"
                   "[build]\n"
                   "dir = \"build\"\n");
    write_new_file(dir / "src" / "main.dd",
                   "def main() -> i32:\n"
                   "    print(\"hello from dudu\")\n"
                   "    return 0\n");
    write_new_file(dir / "README.md", "# " + name + "\n\nRun with:\n\n```bash\ndudu run\n```\n");
    if (write_gitignore) {
        write_new_file(dir / ".gitignore", "build/\n");
    }
}

void new_project(const std::filesystem::path& dir) {
    if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir)) {
        fail("project directory is not empty: " + dir.string());
    }
    std::filesystem::create_directories(dir);
    init_project(dir);
}

void print_project_step(bool enabled, const std::string& label, const std::filesystem::path& path) {
    if (enabled) {
        if (project_step_timings) {
            std::cerr << elapsed_prefix();
        }
        std::cerr << label << ' ' << path.string() << '\n';
    }
}

void set_project_step_timings(bool enabled) {
    project_step_timings = enabled;
    project_step_start = std::chrono::steady_clock::now();
}

bool project_step_timings_enabled() {
    return project_step_timings;
}

} // namespace dudu
