#include "dudu/cmake_backend.hpp"

#include "dudu/cmake_emit.hpp"
#include "dudu/file_io.hpp"
#include "dudu/native_build.hpp"
#include "dudu/project_driver.hpp"
#include "dudu/source.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

class BackendLock {
  public:
    explicit BackendLock(const std::filesystem::path& root) : lock_dir_(root / ".dudu.lock") {
        std::filesystem::create_directories(root);
        constexpr int max_attempts = 2400;
        for (int attempt = 0; attempt < max_attempts; ++attempt) {
            if (std::filesystem::create_directory(lock_dir_)) {
                locked_ = true;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        fail("CMake backend is locked by another dudu process: " + lock_dir_.string());
    }

    BackendLock(const BackendLock&) = delete;
    BackendLock& operator=(const BackendLock&) = delete;

    ~BackendLock() {
        if (!locked_) {
            return;
        }
        std::error_code ignored;
        std::filesystem::remove_all(lock_dir_, ignored);
    }

  private:
    std::filesystem::path lock_dir_;
    bool locked_ = false;
};

std::string read_text_file(const std::filesystem::path& path) {
    return try_read_text_file(path).value_or("");
}

bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
    if (read_text_file(path) == text) {
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        fail("could not open output " + path.string());
    }
    out << text;
    return true;
}

bool configure_is_current(const std::filesystem::path& build_dir,
                          const std::filesystem::path& command_path, const std::string& command) {
    return std::filesystem::exists(build_dir / "CMakeCache.txt") &&
           read_text_file(command_path) == command;
}

void record_configure_command(const std::filesystem::path& command_path,
                              const std::string& command) {
    (void)write_text_file(command_path, command);
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

std::filesystem::path generated_target_artifact(const ProjectConfig& config,
                                                const std::filesystem::path& build_dir,
                                                const std::string& target) {
    if (config.target_kind == "library") {
        return build_dir / ("lib" + target + ".a");
    }
    if (config.target_kind == "shared_library") {
        return build_dir / ("lib" + target + ".so");
    }
    return build_dir / target;
}

std::filesystem::path copy_output_artifact(const std::filesystem::path& source,
                                           const std::filesystem::path& output) {
    std::error_code error;
    if (std::filesystem::equivalent(source, output, error)) {
        return output;
    }
    std::filesystem::create_directories(output.parent_path().empty() ? "." : output.parent_path());
    std::filesystem::copy_file(source, output, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::permissions(output, std::filesystem::status(source).permissions());
    return output;
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
    const std::filesystem::path configure_command_log = options.root / "configure.command";
    if (options.verbose) {
        std::cerr << configure_command << '\n';
    }
    print_project_step(options.stream_output, "configure", build_dir);
    if (configure_is_current(build_dir, configure_command_log, configure_command)) {
        return;
    }
    const int status = options.stream_output
                           ? run_shell_command_streaming(configure_command, configure_log)
                           : run_shell_command(configure_command, configure_log);
    if (status != 0) {
        fail(command_failure_message("CMake configure", configure_command, configure_log));
    }
    record_configure_command(configure_command_log, configure_command);
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
    print_project_step(options.stream_output, "compile", build_dir);
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

std::string output_library_target_name(const std::filesystem::path& output) {
    std::string name = output.stem().string();
    if (name.rfind("lib", 0) == 0 && name.size() > 3) {
        name = name.substr(3);
    }
    return name.empty() ? output.stem().string() : name;
}

std::string generated_cmake_target_name(const ProjectConfig& config,
                                        const std::filesystem::path& input,
                                        const std::optional<std::filesystem::path>& output) {
    if (output.has_value() &&
        (config.target_kind == "library" || config.target_kind == "shared_library")) {
        return output_library_target_name(*output);
    }
    return cmake_target_name(config, input);
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
    return !config.cmake_source.empty();
}

std::filesystem::path run_cmake_backend(const CMakeBackendOptions& options) {
    const BackendLock lock(options.root);
    const std::filesystem::path source_dir = options.root / "source";
    const std::filesystem::path build_dir = options.root / "build";
    const std::filesystem::path cmake_lists = source_dir / "CMakeLists.txt";
    print_project_step(options.stream_output, "generate", cmake_lists);
    const bool cmake_lists_changed = write_text_file(cmake_lists, options.cmake_lists);

    std::string configure_command =
        "cmake -S " + shell_quote_path(source_dir) + " -B " + shell_quote_path(build_dir) +
        " -DDUDU_EXECUTABLE=" +
        shell_quote_path(std::filesystem::absolute(options.dudu_executable));
    configure_command += options.timings ? " -DDUDU_TIMINGS=ON" : " -DDUDU_TIMINGS=OFF";
    const std::filesystem::path configure_log = options.root / "configure.log";
    const std::filesystem::path configure_command_log = options.root / "configure.command";
    if (options.verbose) {
        std::cerr << configure_command << '\n';
    }
    print_project_step(options.stream_output, "configure", build_dir);
    if (cmake_lists_changed ||
        !configure_is_current(build_dir, configure_command_log, configure_command)) {
        const int configure_status =
            options.stream_output ? run_shell_command_streaming(configure_command, configure_log)
                                  : run_shell_command(configure_command, configure_log);
        if (configure_status != 0) {
            fail(command_failure_message("CMake configure", configure_command, configure_log));
        }
        record_configure_command(configure_command_log, configure_command);
    }

    const std::string build_command = "cmake --build " + shell_quote_path(build_dir) +
                                      " --target " + shell_quote_arg(options.target);
    const std::filesystem::path build_log = options.root / "build.log";
    if (options.verbose) {
        std::cerr << build_command << '\n';
    }
    print_project_step(options.stream_output, "compile", build_dir);
    const int build_status = options.stream_output
                                 ? run_shell_command_streaming(build_command, build_log)
                                 : run_shell_command(build_command, build_log);
    if (build_status != 0) {
        fail(command_failure_message("CMake build", build_command, build_log));
    }
    return generated_target_artifact(options.config, build_dir, options.target);
}

std::filesystem::path run_user_cmake_backend(const UserCMakeBackendOptions& options) {
    if (!uses_user_cmake_backend(options.config)) {
        fail("user-owned CMake backend requires [cmake] source");
    }
    const BackendLock lock(options.root);
    const std::filesystem::path build_dir = options.root / "build";
    const std::string target = user_cmake_target_name(options.config);

    configure_user_cmake(options, build_dir);
    build_user_cmake(options, build_dir, target);
    return build_dir / target;
}

int run_user_cmake_tests(const UserCMakeBackendOptions& options) {
    if (!uses_user_cmake_backend(options.config)) {
        fail("user-owned CMake tests require [cmake] source");
    }
    const BackendLock lock(options.root);
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
    const int status = options.stream_output ? run_shell_command_streaming(test_command, test_log)
                                             : run_shell_command(test_command, test_log);
    if (status != 0) {
        fail(command_failure_message("CTest", test_command, test_log));
    }
    if (!options.stream_output) {
        std::cout << read_text_file(test_log);
    }
    return 0;
}

std::filesystem::path build_cmake_project(const BuildCMakeProjectOptions& options) {
    if (uses_user_cmake_backend(options.config)) {
        if (options.output.has_value()) {
            fail("user-owned CMake backend does not support -o; configure the target in CMake");
        }
        return run_user_cmake_backend({.config = options.config,
                                       .root = default_user_cmake_backend_root(options.config),
                                       .stream_output = options.stream_output,
                                       .verbose = options.verbose});
    }
    const std::string target =
        generated_cmake_target_name(options.config, options.input, options.output);
    const std::filesystem::path artifact =
        run_cmake_backend({.config = options.config,
                           .root = default_cmake_backend_root(options.config),
                           .cmake_lists = emit_cmake_project(options.config, options.input, target),
                           .target = target,
                           .dudu_executable = options.dudu_executable,
                           .stream_output = options.stream_output,
                           .timings = options.timings,
                           .verbose = options.verbose});
    if (options.output.has_value()) {
        return copy_output_artifact(artifact, *options.output);
    }
    return artifact;
}

} // namespace dudu
