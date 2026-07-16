#include "dudu/frontend/cli_command.hpp"

#include "dudu/frontend/cli_command_internal.hpp"
#include "dudu/frontend/cli_options.hpp"
#include "dudu/project/project_driver.hpp"
#include "dudu/support/toolchain_manager.hpp"
#include "dudu/testing/test_driver.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

} // namespace

int run_cli(int argc, char** argv) {
    const std::string executable =
        argc > 0 ? std::filesystem::path(argv[0]).stem().string() : "duc";
    const bool project_driver = executable == "dudu";
    const CliOptions options = resolve_project_input(parse_cli_options(argc, argv, project_driver));
    set_project_step_timings(options.timings);

    if (options.init_project) {
        init_project(options.input.empty() ? std::filesystem::path(".") : options.input);
        return 0;
    }
    if (options.new_project) {
        if (options.input.empty()) {
            fail("dudu new requires a project name");
        }
        new_project(options.input);
        return 0;
    }
    if (options.bench) {
        return run_project_benchmarks(options);
    }
    if (options.clean) {
        const std::filesystem::path cleaned =
            clean_project(options.input.empty() ? std::filesystem::path(".") : options.input);
        if (!options.quiet) {
            std::cerr << "clean " << cleaned.string() << '\n';
        }
        return 0;
    }
    if (options.clean_cache) {
        return run_clean_native_cache_command(options);
    }
    if (options.deps_fetch) {
        return run_deps_fetch_command(options);
    }
    if (options.toolchain_update) {
        return run_toolchain_manager(cli_executable_path(argv[0]), "update", options.command_args);
    }
    if (options.uninstall) {
        return run_toolchain_manager(cli_executable_path(argv[0]), "uninstall",
                                     options.command_args);
    }
    if (options.test) {
        return run_project_tests({.input = options.input,
                                  .output = options.output,
                                  .build_values = options.build_values,
                                  .compiler_executable = cli_compiler_path(argv[0]),
                                  .target_name = options.target_name,
                                  .test_filter = options.test_filter,
                                  .no_capture = options.no_capture,
                                  .project_driver = options.project_driver,
                                  .quiet = options.quiet,
                                  .timings = options.timings,
                                  .verbose = options.verbose});
    }
    if (options.format) {
        return run_format_command(options);
    }
    if (options.cmake) {
        return run_cmake_command(options);
    }
    if (options.build) {
        return run_build_command(options, argv[0]);
    }
    if (options.run) {
        return run_run_command(options, argv[0]);
    }
    return run_compile_command(options, argv[0]);
}

} // namespace dudu
