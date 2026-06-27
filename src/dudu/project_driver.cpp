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

std::string cmake_identifier(std::string name) {
    if (name.empty()) {
        return "dudu_app";
    }
    for (char& c : name) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!valid) {
            c = '_';
        }
    }
    if (name.front() >= '0' && name.front() <= '9') {
        name.insert(name.begin(), '_');
    }
    return name;
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

std::string scaffold_cmake_lists(const std::string& name) {
    const std::string cmake_name = cmake_identifier(name);
    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.20)\n"
           "\n"
        << "project(" << cmake_name
        << " LANGUAGES CXX)\n"
           "\n"
           "set(DUC_EXECUTABLE \"\" CACHE FILEPATH \"Path to the duc compiler\")\n"
           "if(NOT DUC_EXECUTABLE)\n"
           "    find_program(DUC_EXECUTABLE_FOUND duc REQUIRED)\n"
           "    set(DUC_EXECUTABLE \"${DUC_EXECUTABLE_FOUND}\" CACHE FILEPATH "
           "\"Path to the duc compiler\" FORCE)\n"
           "endif()\n"
           "\n"
           "set(DUDU_ENTRY \"${CMAKE_CURRENT_SOURCE_DIR}/src/main.dd\")\n"
           "set(DUDU_MANIFEST \"${CMAKE_CURRENT_SOURCE_DIR}/dudu.toml\")\n"
           "set(DUDU_GENERATED_DIR \"${CMAKE_CURRENT_BINARY_DIR}/dudu-generated\")\n"
           "set(DUDU_GENERATED_STAMP \"${DUDU_GENERATED_DIR}/.dudu_emit.stamp\")\n"
           "file(GLOB_RECURSE DUDU_SOURCES CONFIGURE_DEPENDS "
           "\"${CMAKE_CURRENT_SOURCE_DIR}/src/*.dd\")\n"
           "execute_process(\n"
           "    COMMAND \"${CMAKE_COMMAND}\" -E make_directory \"${DUDU_GENERATED_DIR}\"\n"
           "    COMMAND \"${DUC_EXECUTABLE}\" emit-modules \"${DUDU_ENTRY}\" -o "
           "\"${DUDU_GENERATED_DIR}\"\n"
           "    WORKING_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}\"\n"
           "    COMMAND_ERROR_IS_FATAL ANY\n"
           ")\n"
           "file(GLOB_RECURSE DUDU_GENERATED_SOURCES CONFIGURE_DEPENDS "
           "\"${DUDU_GENERATED_DIR}/*.cpp\")\n"
           "\n"
           "add_custom_command(\n"
           "    OUTPUT \"${DUDU_GENERATED_STAMP}\"\n"
           "    BYPRODUCTS ${DUDU_GENERATED_SOURCES}\n"
           "    COMMAND \"${CMAKE_COMMAND}\" -E make_directory \"${DUDU_GENERATED_DIR}\"\n"
           "    COMMAND \"${DUC_EXECUTABLE}\" emit-modules \"${DUDU_ENTRY}\" -o "
           "\"${DUDU_GENERATED_DIR}\"\n"
           "    COMMAND \"${CMAKE_COMMAND}\" -E touch \"${DUDU_GENERATED_STAMP}\"\n"
           "    DEPENDS \"${DUC_EXECUTABLE}\" \"${DUDU_MANIFEST}\" ${DUDU_SOURCES}\n"
           "    WORKING_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}\"\n"
           "    VERBATIM\n"
           ")\n"
           "add_custom_target(dudu_generate DEPENDS \"${DUDU_GENERATED_STAMP}\")\n"
           "\n";
    out << "add_executable(" << cmake_name << " ${DUDU_GENERATED_SOURCES})\n"
        << "add_dependencies(" << cmake_name << " dudu_generate)\n"
        << "target_include_directories(" << cmake_name << " PRIVATE \"${DUDU_GENERATED_DIR}\")\n"
        << "target_compile_features(" << cmake_name << " PRIVATE cxx_std_20)\n";
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
    const std::string manifest = "name = \"" + name +
                                 "\"\n"
                                 "entry = \"src/main.dd\"\n"
                                 "\n"
                                 "[cxx]\n"
                                 "standard = \"c++20\"\n"
                                 "\n"
                                 "[build]\n"
                                 "dir = \"build\"\n";
    write_new_file(dir / "dudu.toml", manifest);
    write_new_file(dir / "src" / "main.dd", "def main() -> i32:\n"
                                            "    print(\"hello from dudu\")\n"
                                            "    return 0\n");
    write_new_file(dir / "README.md", "# " + name + "\n\nRun with:\n\n```bash\ndudu run\n```\n");
    write_new_file(dir / "CMakeLists.txt", scaffold_cmake_lists(name));
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
