#include "dudu/cmake_backend.hpp"

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

} // namespace

std::filesystem::path default_cmake_backend_root(const ProjectConfig& config) {
    const std::filesystem::path build_dir = config.build_dir.empty() ? "build" : config.build_dir;
    return project_path(config, build_dir) / "cmake-backend";
}

std::string cmake_target_name(const ProjectConfig& config, const std::filesystem::path& input) {
    return config.name.empty() ? input.stem().string() : config.name;
}

std::filesystem::path run_cmake_backend(const CMakeBackendOptions& options) {
    const std::filesystem::path source_dir = options.root / "source";
    const std::filesystem::path build_dir = options.root / "build";
    const std::filesystem::path cmake_lists = source_dir / "CMakeLists.txt";
    write_text_file(cmake_lists, options.cmake_lists);

    const std::string configure_command =
        "cmake -S " + shell_quote_path(source_dir) + " -B " + shell_quote_path(build_dir) +
        " -DDUDU_EXECUTABLE=" +
        shell_quote_path(std::filesystem::absolute(options.dudu_executable));
    const std::filesystem::path configure_log = options.root / "configure.log";
    if (options.verbose) {
        std::cerr << configure_command << '\n';
    }
    if (run_shell_command(configure_command, configure_log) != 0) {
        fail(command_failure_message("CMake configure", configure_command, configure_log));
    }

    const std::string build_command = "cmake --build " + shell_quote_path(build_dir) +
                                      " --target " + shell_quote_arg(options.target);
    const std::filesystem::path build_log = options.root / "build.log";
    if (options.verbose) {
        std::cerr << build_command << '\n';
    }
    if (run_shell_command(build_command, build_log) != 0) {
        fail(command_failure_message("CMake build", build_command, build_log));
    }
    return build_dir / options.target;
}

} // namespace dudu
