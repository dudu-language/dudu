#include "dudu/cli_options.hpp"

#include "dudu/cli_usage.hpp"
#include "dudu/cmake_backend.hpp"
#include "dudu/project_config.hpp"

#include <cstdlib>
#include <stdexcept>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void add_build_value(CliOptions& options, const std::string& define) {
    const size_t equal = define.find('=');
    if (equal == std::string::npos || equal == 0) {
        fail("-D requires NAME=value");
    }
    options.build_values[define.substr(0, equal)] = define.substr(equal + 1);
}

} // namespace

std::filesystem::path build_config_path(const std::filesystem::path& input) {
    return find_project_config(input);
}

CliOptions parse_cli_options(int argc, char** argv, bool project_driver) {
    CliOptions options;
    options.project_driver = project_driver;
    int first_arg = 1;
    if (project_driver && argc > 1 && std::string(argv[1]) == "init") {
        options.init_project = true;
        first_arg = 2;
    } else if (project_driver && argc > 1 && std::string(argv[1]) == "new") {
        options.new_project = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "build") {
        options.build = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "bench") {
        options.bench = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "check") {
        options.check = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "clean") {
        options.clean = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "clean-cache") {
        options.clean_cache = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "cmake") {
        options.cmake = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "emit") {
        options.emit_cpp = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "emit-modules") {
        options.emit_modules = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "emit-test-modules") {
        options.emit_test_modules = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "fmt") {
        options.format = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "run") {
        options.run = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "test") {
        options.test = true;
        first_arg = 2;
    }

    for (int i = first_arg; i < argc; ++i) {
        const std::string arg = argv[i];
        if (options.bench) {
            if (!project_driver) {
                options.command_args.push_back(arg);
                continue;
            }
            if (arg == "--") {
                while (++i < argc) {
                    options.command_args.push_back(argv[i]);
                }
                break;
            }
            if (arg == "-h" || arg == "--help") {
                print_cli_usage(project_driver);
                std::exit(0);
            }
            if (arg == "--version") {
                print_cli_version(project_driver);
                std::exit(0);
            }
            if (arg == "--verbose") {
                options.verbose = true;
                continue;
            }
            if (arg == "--quiet") {
                options.quiet = true;
                continue;
            }
            if (arg == "--timings") {
                options.timings = true;
                continue;
            }
            options.command_args.push_back(arg);
            continue;
        }
        if (options.run && arg == "--") {
            while (++i < argc) {
                options.command_args.push_back(argv[i]);
            }
            break;
        }
        if (arg == "-h" || arg == "--help") {
            print_cli_usage(project_driver);
            std::exit(0);
        }
        if (arg == "--version") {
            print_cli_version(project_driver);
            std::exit(0);
        }
        if (arg == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (arg == "--quiet") {
            options.quiet = true;
            continue;
        }
        if (arg == "--timings") {
            options.timings = true;
            continue;
        }
        if (arg == "--filter") {
            if (i + 1 >= argc) {
                fail("--filter requires text");
            }
            options.test_filter = argv[++i];
            continue;
        }
        if (arg == "--no-capture" || arg == "--nocapture") {
            options.no_capture = true;
            continue;
        }
        if (arg == "-o") {
            if (i + 1 >= argc) {
                fail("-o requires a path");
            }
            options.output = argv[++i];
            continue;
        }
        if (arg == "-D") {
            if (i + 1 >= argc) {
                fail("-D requires NAME=value");
            }
            add_build_value(options, argv[++i]);
            continue;
        }
        if (arg.size() > 2 && arg.substr(0, 2) == "-D") {
            add_build_value(options, arg.substr(2));
            continue;
        }
        if (arg == "--check") {
            options.check = true;
            continue;
        }
        if (arg == "--emit-cpp") {
            if (i + 1 >= argc) {
                fail("--emit-cpp requires a path or '-'");
            }
            options.emit_cpp = true;
            const std::string value = argv[++i];
            if (value != "-") {
                options.output = value;
            }
            continue;
        }
        if (arg == "--emit-header") {
            if (i + 1 >= argc) {
                fail("--emit-header requires a path or '-'");
            }
            const std::string value = argv[++i];
            options.header_output =
                value == "-" ? std::filesystem::path{} : std::filesystem::path{value};
            continue;
        }
        if (arg == "--emit-c-header") {
            if (i + 1 >= argc) {
                fail("--emit-c-header requires a path or '-'");
            }
            const std::string value = argv[++i];
            options.c_header_output =
                value == "-" ? std::filesystem::path{} : std::filesystem::path{value};
            continue;
        }
        if (arg == "--format") {
            if (i + 1 >= argc) {
                fail("--format requires a path or '-'");
            }
            options.format = true;
            const std::string value = argv[++i];
            if (value != "-") {
                options.output = value;
            }
            continue;
        }
        if (options.input.empty()) {
            options.input = arg;
            continue;
        }
        fail("unexpected argument: " + arg);
    }
    const bool project_format_default = options.project_driver && options.format;
    if (options.input.empty() && !project_format_default && !options.bench && !options.build &&
        !options.check && !options.clean && !options.clean_cache && !options.cmake &&
        !options.emit_cpp && !options.emit_modules && !options.init_project &&
        !options.new_project && !options.run && !options.test) {
        fail("missing input file");
    }
    return options;
}

CliOptions resolve_project_input(CliOptions options) {
    if (options.bench || options.clean || options.clean_cache || options.init_project ||
        options.new_project || options.test) {
        return options;
    }
    if (options.project_driver && options.format && options.input.empty()) {
        options.input = ".";
        return options;
    }
    const std::filesystem::path config_path = options.input.empty()
                                                  ? std::filesystem::path("dudu.toml")
                                                  : build_config_path(options.input);
    const ProjectConfig project = parse_project_config(config_path);
    const bool command_uses_project_entry =
        options.build || options.run || options.cmake || options.emit_cpp || options.emit_modules;
    if (!options.input.empty()) {
        const std::string input = options.input.string();
        if (!std::filesystem::exists(options.input) && options.input.extension() != ".dd" &&
            project.targets.contains(input)) {
            options.target_name = input;
            options.input = project_path(project, apply_project_target(project, input).main);
            if (options.input.empty()) {
                fail("target has no entry: " + input);
            }
        } else if (std::filesystem::exists(options.input)) {
            if (std::filesystem::is_directory(options.input) && command_uses_project_entry) {
                if (project.main.empty()) {
                    if ((options.build || options.run) && uses_user_cmake_backend(project)) {
                        return options;
                    }
                    fail("missing input file and dudu.toml main");
                }
                options.input = project_path(project, project.main);
                return options;
            }
            const std::filesystem::path normalized_input = options.input.lexically_normal();
            for (const auto& [name, target] : project.targets) {
                const std::filesystem::path target_input =
                    project_path(project, target.main).lexically_normal();
                const std::filesystem::path input_path =
                    std::filesystem::absolute(normalized_input).lexically_normal();
                if (!target.main.empty() && target_input == input_path) {
                    options.target_name = name;
                    break;
                }
            }
        }
        return options;
    }
    if (project.main.empty()) {
        if ((options.build || options.run) && uses_user_cmake_backend(project)) {
            return options;
        }
        fail("missing input file and dudu.toml main");
    }
    options.input = project_path(project, project.main);
    return options;
}

} // namespace dudu
