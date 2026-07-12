#include "dudu/frontend/cli_options.hpp"
#include "dudu/project/cmake_emit.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_dependencies.hpp"

#include <cassert>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

namespace {

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("could not write " + path.string());
    }
    out << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("could not read " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<char*> argv_for(std::vector<std::string>& args) {
    std::vector<char*> out;
    out.reserve(args.size());
    for (std::string& arg : args) {
        out.push_back(arg.data());
    }
    return out;
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
    assert(config.manifest_path.lexically_normal() ==
           std::filesystem::absolute(project / "dudu.toml").lexically_normal());
    assert(dudu::project_path(config, "third_party/include").lexically_normal() ==
           (project / "third_party/include").lexically_normal());
}

void test_project_dependency_manifest_and_lockfile(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-deps";
    const std::filesystem::path dependency = root / "build" / "project-config-deps-local";
    std::filesystem::remove_all(project);
    std::filesystem::remove_all(dependency);
    write_text(project / "dudu.toml", "name = \"deps_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[deps]\n"
                                      "local_math = { path = \"../project-config-deps-local\" }\n");
    write_text(project / "src" / "main.dd", "from local_math import value\n");
    write_text(dependency / "dudu.toml", "name = \"local_math\"\n"
                                         "entry = \"src/local_math.dd\"\n");
    write_text(dependency / "src" / "local_math.dd", "def value() -> i32:\n"
                                                     "    return 4\n");

    dudu::ProjectConfig config = dudu::parse_project_config(project / "dudu.toml");
    assert(config.dependencies.size() == 1);
    assert(config.dependencies.at("local_math").kind == "path");
    assert(config.dependencies.at("local_math").path == "../project-config-deps-local");
    const std::map<std::string, std::filesystem::path> roots =
        dudu::dependency_module_roots(config);
    assert(roots.at("local_math").lexically_normal() == (dependency / "src").lexically_normal());

    dudu::ensure_project_dependencies(config, false, true);
    const std::string lock = read_text(project / "dudu.lock");
    assert(lock.find("name = \"local_math\"") != std::string::npos);
    assert(lock.find("kind = \"path\"") != std::string::npos);

    write_text(project / "dudu.toml",
               "name = \"deps_probe\"\n"
               "\n"
               "[deps]\n"
               "bad = { path = \"x\", git = \"https://example.invalid/x\" }\n");
    bool rejected = false;
    try {
        (void)dudu::parse_project_config(project / "dudu.toml");
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()).find("exactly one of path or git") != std::string::npos;
    }
    assert(rejected);
}

void test_cmake_emit_depends_on_manifest(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-cmake-depends";
    std::filesystem::remove_all(project);
    write_text(project / "dudu.toml", "name = \"manifest_dep_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[build]\n"
                                      "MODE = \"fast\"\n");
    write_text(project / "src" / "main.dd", "def main() -> i32:\n    return 0\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(project / "dudu.toml");
    const std::string cmake = dudu::emit_cmake_project(config, project / "src" / "main.dd");
    assert(cmake.find((project / "dudu.toml").string()) != std::string::npos);
    assert(cmake.find("DEPENDS ${DUDU_EXECUTABLE}") != std::string::npos);
    assert(cmake.find("CONTENT \"$<TARGET_FILE:manifest_dep_probe>\"") != std::string::npos);

    const std::string test_cmake = dudu::emit_cmake_test_project(
        config, project / "src" / "main.dd", "manifest_dep_probe_tests", "", true);
    assert(test_cmake.find("CONTENT \"$<TARGET_FILE:manifest_dep_probe_tests>\"") !=
           std::string::npos);
}

void test_cmake_project_config(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-cmake";
    std::filesystem::remove_all(project);
    write_text(project / "dudu.toml", "name = \"cmake_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[build]\n"
                                      "dir = \"build\"\n"
                                      "\n"
                                      "[cmake]\n"
                                      "source = \".\"\n"
                                      "target = \"cmake_probe\"\n"
                                      "config = \"Debug\"\n"
                                      "generator = \"Ninja\"\n");
    write_text(project / "src" / "main.dd", "def main() -> i32:\n    return 0\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(project / "dudu.toml");
    assert(config.build_dir == "build");
    assert(config.cmake_source == ".");
    assert(config.cmake_target == "cmake_probe");
    assert(config.cmake_config == "Debug");
    assert(config.cmake_generator == "Ninja");

    write_text(project / "dudu.toml", "name = \"cmake_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[build]\n"
                                      "backend = \"nonsense\"\n");
    bool rejected = false;
    try {
        (void)dudu::parse_project_config(project / "dudu.toml");
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()).find("[build] backend was removed") != std::string::npos;
    }
    assert(rejected);

    write_text(project / "dudu.toml", "name = \"cmake_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[build]\n"
                                      "backend = \"direct\"\n");
    bool direct_rejected = false;
    try {
        (void)dudu::parse_project_config(project / "dudu.toml");
    } catch (const std::runtime_error&) {
        direct_rejected = true;
    }
    assert(direct_rejected);

    write_text(project / "dudu.toml", "name = \"cmake_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[cmake]\n"
                                      "enabled = true\n");
    bool stale_enabled_rejected = false;
    try {
        (void)dudu::parse_project_config(project / "dudu.toml");
    } catch (const std::runtime_error& error) {
        stale_enabled_rejected =
            std::string(error.what()).find("unknown [cmake] entry") != std::string::npos;
    }
    assert(stale_enabled_rejected);
}

void test_quoted_manifest_strings(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-quoted-strings";
    std::filesystem::remove_all(project);
    write_text(project / "dudu.toml",
               "name = \"quoted_probe\"\n"
               "entry = \"src/main.dd\"\n"
               "\n"
               "[bench]\n"
               "command = \"printf \\\"hello\\\" && printf C:\\\\tmp\"\n"
               "\n"
               "[include]\n"
               "paths = [\"include/with\\\\slash\", \"include/with\\\"quote\"]\n");
    write_text(project / "src" / "main.dd", "def main() -> i32:\n    return 0\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(project / "dudu.toml");
    assert(config.bench_command == "printf \"hello\" && printf C:\\tmp");
    assert(config.include_dirs.size() == 2);
    assert(config.include_dirs[0] == "include/with\\slash");
    assert(config.include_dirs[1] == "include/with\"quote");
}

void test_invalid_manifest_string_escapes(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-invalid-escapes";
    std::filesystem::remove_all(project);
    write_text(project / "src" / "main.dd", "def main() -> i32:\n    return 0\n");

    write_text(project / "dudu.toml", "name = \"bad\\qescape\"\n"
                                      "entry = \"src/main.dd\"\n");
    bool rejected_scalar = false;
    try {
        (void)dudu::parse_project_config(project / "dudu.toml");
    } catch (const std::runtime_error& error) {
        rejected_scalar =
            std::string(error.what()).find("invalid string escape") != std::string::npos;
    }
    assert(rejected_scalar);

    write_text(project / "dudu.toml", "name = \"bad_array_escape\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[include]\n"
                                      "paths = [\"ok\", \"bad\\qpath\"]\n");
    bool rejected_array = false;
    try {
        (void)dudu::parse_project_config(project / "dudu.toml");
    } catch (const std::runtime_error& error) {
        rejected_array =
            std::string(error.what()).find("invalid string escape") != std::string::npos;
    }
    assert(rejected_array);

    write_text(project / "dudu.toml", "name = \"bad_trailing_escape\\\"\n"
                                      "entry = \"src/main.dd\"\n");
    bool rejected_trailing = false;
    try {
        (void)dudu::parse_project_config(project / "dudu.toml");
    } catch (const std::runtime_error& error) {
        rejected_trailing =
            std::string(error.what()).find("unterminated string escape") != std::string::npos;
    }
    assert(rejected_trailing);
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

void test_project_driver_command_defaults(const std::filesystem::path& root) {
    const std::filesystem::path project = root / "build" / "project-config-command-defaults";
    const std::filesystem::path outside = root / "build" / "project-config-command-outside";
    std::filesystem::remove_all(project);
    std::filesystem::remove_all(outside);
    std::filesystem::create_directories(outside);
    write_text(project / "dudu.toml", "name = \"defaults_probe\"\n"
                                      "entry = \"src/main.dd\"\n"
                                      "\n"
                                      "[build]\n"
                                      "dir = \"out\"\n");
    write_text(project / "src" / "main.dd", "def main() -> i32:\n    return 0\n");

    const std::filesystem::path original_cwd = std::filesystem::current_path();
    try {
        std::filesystem::current_path(project);

        std::vector<std::string> fmt_args = {"dudu", "fmt"};
        std::vector<char*> fmt_argv = argv_for(fmt_args);
        dudu::CliOptions fmt =
            dudu::parse_cli_options(static_cast<int>(fmt_argv.size()), fmt_argv.data(), true);
        fmt = dudu::resolve_project_input(std::move(fmt));
        assert(fmt.project_driver);
        assert(fmt.format);
        assert(fmt.input == ".");

        std::vector<std::string> check_args = {"dudu", "check"};
        std::vector<char*> check_argv = argv_for(check_args);
        dudu::CliOptions check =
            dudu::parse_cli_options(static_cast<int>(check_argv.size()), check_argv.data(), true);
        check = dudu::resolve_project_input(std::move(check));
        assert(check.project_driver);
        assert(check.check);
        assert(check.input.lexically_normal() == (project / "src" / "main.dd").lexically_normal());

        std::filesystem::current_path(outside);
        std::vector<std::string> build_args = {"dudu", "build", project.string()};
        std::vector<char*> build_argv = argv_for(build_args);
        dudu::CliOptions build =
            dudu::parse_cli_options(static_cast<int>(build_argv.size()), build_argv.data(), true);
        build = dudu::resolve_project_input(std::move(build));
        assert(build.project_driver);
        assert(build.build);
        assert(build.input.lexically_normal() == (project / "src" / "main.dd").lexically_normal());
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
        test_project_dependency_manifest_and_lockfile(DUDU_REPO_ROOT);
        test_cmake_emit_depends_on_manifest(DUDU_REPO_ROOT);
        test_cmake_project_config(DUDU_REPO_ROOT);
        test_quoted_manifest_strings(DUDU_REPO_ROOT);
        test_invalid_manifest_string_escapes(DUDU_REPO_ROOT);
        test_project_driver_resolves_manifest_relative_entries(DUDU_REPO_ROOT);
        test_project_driver_command_defaults(DUDU_REPO_ROOT);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
