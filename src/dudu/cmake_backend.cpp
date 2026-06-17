#include "dudu/cmake_backend.hpp"

#include "dudu/cmake_emit.hpp"
#include "dudu/native_build.hpp"
#include "dudu/source.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
    std::ofstream out(path);
    if (!out) {
        fail("could not open output " + path.string());
    }
    out << text;
}

void print_stage(bool enabled, const std::string& label, const std::filesystem::path& path) {
    if (enabled) {
        std::cerr << label << " " << path.string() << '\n';
    }
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string command_failure_message(const std::string& label, const std::string& command,
                                    const std::filesystem::path& log_path) {
    std::string message = label + " failed\ncommand: " + command;
    const std::string output = read_text_file(log_path);
    if (!output.empty()) {
        message += "\noutput:\n" + output;
    }
    return message;
}

void configure_user_cmake(const UserCMakeBackendOptions& options,
                          const std::filesystem::path& build_dir) {
    const std::filesystem::path source_dir =
        project_path(options.config, options.config.cmake_source);
    std::string configure_command =
        "cmake -S " + shell_quote_path(source_dir) + " -B " + shell_quote_path(build_dir);
    if (!options.config.cmake_generator.empty()) {
        configure_command += " -G " + shell_quote_arg(options.config.cmake_generator);
    }
    if (!options.config.cmake_config.empty()) {
        configure_command += " -DCMAKE_BUILD_TYPE=" + shell_quote_arg(options.config.cmake_config);
    }
    const std::filesystem::path configure_log = options.root / "configure.log";
    if (options.verbose) {
        std::cerr << configure_command << '\n';
    }
    print_stage(options.stream_output, "configure", build_dir);
    const int status = options.stream_output
                           ? run_shell_command_streaming(configure_command, configure_log)
                           : run_shell_command(configure_command, configure_log);
    if (status != 0) {
        fail(command_failure_message("CMake configure", configure_command, configure_log));
    }
}

void build_user_cmake(const UserCMakeBackendOptions& options,
                      const std::filesystem::path& build_dir,
                      const std::optional<std::string>& target) {
    std::string build_command = "cmake --build " + shell_quote_path(build_dir);
    if (target.has_value()) {
        build_command += " --target " + shell_quote_arg(*target);
    }
    if (!options.config.cmake_config.empty()) {
        build_command += " --config " + shell_quote_arg(options.config.cmake_config);
    }
    const std::filesystem::path build_log = options.root / "build.log";
    if (options.verbose) {
        std::cerr << build_command << '\n';
    }
    print_stage(options.stream_output, "compile", build_dir);
    const int status = options.stream_output ? run_shell_command_streaming(build_command, build_log)
                                             : run_shell_command(build_command, build_log);
    if (status != 0) {
        fail(command_failure_message("CMake build", build_command, build_log));
    }
}

} // namespace

std::filesystem::path default_cmake_backend_root(const ProjectConfig& config) {
    const std::filesystem::path build_dir = config.build_dir.empty() ? "build" : config.build_dir;
    return project_path(config, build_dir) / "cmake-backend";
}

std::filesystem::path default_user_cmake_backend_root(const ProjectConfig& config) {
    const std::filesystem::path build_dir = config.build_dir.empty() ? "build" : config.build_dir;
    return project_path(config, build_dir) / "cmake-user";
}

std::filesystem::path cmake_backend_log_source(const ProjectConfig& config) {
    if (uses_user_cmake_backend(config)) {
        return project_path(config, config.cmake_source) / "CMakeLists.txt";
    }
    return default_cmake_backend_root(config) / "source" / "CMakeLists.txt";
}

std::filesystem::path cmake_backend_log_build_dir(const ProjectConfig& config) {
    if (uses_user_cmake_backend(config)) {
        return default_user_cmake_backend_root(config) / "build";
    }
    return default_cmake_backend_root(config) / "build";
}

std::string cmake_target_name(const ProjectConfig& config, const std::filesystem::path& input) {
    return config.name.empty() ? input.stem().string() : config.name;
}

std::string user_cmake_target_name(const ProjectConfig& config) {
    if (!config.cmake_target.empty()) {
        return config.cmake_target;
    }
    if (!config.name.empty()) {
        return config.name;
    }
    fail("user-owned CMake backend requires [cmake] target or top-level name");
}

bool uses_user_cmake_backend(const ProjectConfig& config) {
    return config.build_backend == "cmake" && !config.cmake_source.empty();
}

std::filesystem::path run_cmake_backend(const CMakeBackendOptions& options) {
    const std::filesystem::path source_dir = options.root / "source";
    const std::filesystem::path build_dir = options.root / "build";
    const std::filesystem::path cmake_lists = source_dir / "CMakeLists.txt";
    print_stage(options.stream_output, "generate", cmake_lists);
    write_text_file(cmake_lists, options.cmake_lists);

    const std::string configure_command =
        "cmake -S " + shell_quote_path(source_dir) + " -B " + shell_quote_path(build_dir) +
        " -DDUDU_EXECUTABLE=" +
        shell_quote_path(std::filesystem::absolute(options.dudu_executable));
    const std::filesystem::path configure_log = options.root / "configure.log";
    if (options.verbose) {
        std::cerr << configure_command << '\n';
    }
    print_stage(options.stream_output, "configure", build_dir);
    const int configure_status = options.stream_output
                                     ? run_shell_command_streaming(configure_command, configure_log)
                                     : run_shell_command(configure_command, configure_log);
    if (configure_status != 0) {
        fail(command_failure_message("CMake configure", configure_command, configure_log));
    }

    const std::string build_command = "cmake --build " + shell_quote_path(build_dir) +
                                      " --target " + shell_quote_arg(options.target);
    const std::filesystem::path build_log = options.root / "build.log";
    if (options.verbose) {
        std::cerr << build_command << '\n';
    }
    print_stage(options.stream_output, "compile", build_dir);
    const int build_status = options.stream_output
                                 ? run_shell_command_streaming(build_command, build_log)
                                 : run_shell_command(build_command, build_log);
    if (build_status != 0) {
        fail(command_failure_message("CMake build", build_command, build_log));
    }
    return build_dir / options.target;
}

std::filesystem::path run_user_cmake_backend(const UserCMakeBackendOptions& options) {
    if (!uses_user_cmake_backend(options.config)) {
        fail("user-owned CMake backend requires [build] backend = \"cmake\" and [cmake] source");
    }
    std::filesystem::create_directories(options.root);
    const std::filesystem::path build_dir = options.root / "build";
    const std::string target = user_cmake_target_name(options.config);

    configure_user_cmake(options, build_dir);
    build_user_cmake(options, build_dir, target);
    return build_dir / target;
}

int run_user_cmake_tests(const UserCMakeBackendOptions& options) {
    if (!uses_user_cmake_backend(options.config)) {
        fail("user-owned CMake tests require [build] backend = \"cmake\" and [cmake] source");
    }
    std::filesystem::create_directories(options.root);
    const std::filesystem::path build_dir = options.root / "build";
    configure_user_cmake(options, build_dir);
    build_user_cmake(options, build_dir, std::nullopt);

    std::string test_command = "ctest --test-dir " + shell_quote_path(build_dir);
    if (!options.config.cmake_config.empty()) {
        test_command += " --build-config " + shell_quote_arg(options.config.cmake_config);
    }
    const std::filesystem::path test_log = options.root / "test.log";
    if (options.verbose) {
        std::cerr << test_command << '\n';
    }
    if (run_shell_command(test_command, test_log) != 0) {
        fail(command_failure_message("CTest", test_command, test_log));
    }
    std::cout << read_text_file(test_log);
    return 0;
}

std::filesystem::path build_cmake_project(const BuildCMakeProjectOptions& options) {
    if (options.output.has_value()) {
        fail("CMake backend does not support -o; configure the target name in dudu.toml");
    }
    if (uses_user_cmake_backend(options.config)) {
        return run_user_cmake_backend({.config = options.config,
                                       .root = default_user_cmake_backend_root(options.config),
                                       .stream_output = options.stream_output,
                                       .verbose = options.verbose});
    }
    return run_cmake_backend({.config = options.config,
                              .root = default_cmake_backend_root(options.config),
                              .cmake_lists = emit_cmake_project(options.config, options.input),
                              .target = cmake_target_name(options.config, options.input),
                              .dudu_executable = options.dudu_executable,
                              .stream_output = options.stream_output,
                              .verbose = options.verbose});
}

} // namespace dudu
