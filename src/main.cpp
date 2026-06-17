#include "dudu/cli_usage.hpp"
#include "dudu/cmake_backend.hpp"
#include "dudu/cmake_emit.hpp"
#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_emit_modules.hpp"
#include "dudu/format_path.hpp"
#include "dudu/language_server.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_header_cache.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/project_driver.hpp"
#include "dudu/sema.hpp"
#include "dudu/test_driver.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::optional<std::filesystem::path> c_header_output;
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> header_output;
    std::map<std::string, std::string> build_values;
    std::string target_name;
    std::vector<std::string> command_args;
    std::string test_filter;
    bool bench = false;
    bool build = false;
    bool check = false;
    bool clean = false;
    bool clean_cache = false;
    bool cmake = false;
    bool emit_cpp = false;
    bool emit_modules = false;
    bool format = false;
    bool init_project = false;
    bool lsp = false;
    bool new_project = false;
    bool no_capture = false;
    bool project_driver = false;
    bool run = false;
    bool test = false;
    bool verbose = false;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void add_build_value(Options& options, const std::string& define) {
    const size_t equal = define.find('=');
    if (equal == std::string::npos || equal == 0) {
        fail("-D requires NAME=value");
    }
    options.build_values[define.substr(0, equal)] = define.substr(equal + 1);
}

std::filesystem::path build_config_path(const std::filesystem::path& input) {
    return dudu::find_project_config(input);
}

Options parse_options(int argc, char** argv, bool project_driver) {
    Options options;
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
    } else if (argc > 1 && std::string(argv[1]) == "fmt") {
        options.format = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "lsp") {
        options.lsp = true;
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
            options.command_args.push_back(arg);
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            dudu::print_cli_usage(project_driver);
            std::exit(0);
        }
        if (arg == "--version") {
            dudu::print_cli_version();
            std::exit(0);
        }
        if (arg == "--verbose") {
            options.verbose = true;
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
    if (options.input.empty() && !options.bench && !options.build && !options.check &&
        !options.clean && !options.clean_cache && !options.cmake && !options.emit_cpp &&
        !options.emit_modules && !options.init_project && !options.lsp && !options.new_project &&
        !options.run && !options.test) {
        fail("missing input file");
    }
    return options;
}

Options resolve_project_input(Options options) {
    if (options.bench || options.clean || options.clean_cache || options.init_project ||
        options.lsp || options.new_project || options.test) {
        return options;
    }
    const std::filesystem::path config_path = options.input.empty()
                                                  ? std::filesystem::path("dudu.toml")
                                                  : build_config_path(options.input);
    const dudu::ProjectConfig project = dudu::parse_project_config(config_path);
    if (!options.input.empty()) {
        const std::string input = options.input.string();
        if (!std::filesystem::exists(options.input) && options.input.extension() != ".dd" &&
            project.targets.contains(input)) {
            options.target_name = input;
            options.input = dudu::apply_project_target(project, input).main;
            if (options.input.empty()) {
                fail("target has no entry: " + input);
            }
        } else if (std::filesystem::exists(options.input)) {
            const std::filesystem::path normalized_input = options.input.lexically_normal();
            for (const auto& [name, target] : project.targets) {
                const std::filesystem::path target_input =
                    dudu::project_path(project, target.main).lexically_normal();
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
        fail("missing input file and dudu.toml main");
    }
    options.input = project.main;
    return options;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        fail("could not open " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_text_output(const std::optional<std::filesystem::path>& path, const std::string& text) {
    if (!path.has_value() || path->empty()) {
        std::cout << text;
        return;
    }
    std::ofstream out(*path);
    if (!out) {
        fail("could not open output " + path->string());
    }
    out << text;
}

std::filesystem::path build_cmake_backend(const Options& options, const dudu::ProjectConfig& config,
                                          const std::filesystem::path& executable_path) {
    if (options.output.has_value()) {
        fail("CMake backend does not support -o; configure the target name in dudu.toml");
    }
    return dudu::run_cmake_backend({.config = config,
                                    .root = dudu::default_cmake_backend_root(config),
                                    .cmake_lists = dudu::emit_cmake_project(config, options.input),
                                    .target = dudu::cmake_target_name(config, options.input),
                                    .dudu_executable = executable_path,
                                    .verbose = options.verbose});
}

int run_project_benchmarks(const Options& options) {
    const dudu::ProjectConfig config = dudu::parse_project_config(build_config_path(options.input));
    std::string command = config.bench_command;
    if (command.empty() && std::filesystem::exists("scripts/bench.sh")) {
        command = "./scripts/bench.sh";
    }
    if (command.empty()) {
        fail("missing [bench] command and scripts/bench.sh");
    }
    return std::system(dudu::append_command_args(command, options.command_args).c_str()) == 0 ? 0
                                                                                              : 1;
}

dudu::ProjectConfig config_for_input(const std::filesystem::path& input) {
    return dudu::parse_project_config(build_config_path(input));
}

std::filesystem::path source_dir_for_input(const std::filesystem::path& input) {
    if (input.empty()) {
        return std::filesystem::current_path();
    }
    return input.has_parent_path() ? input.parent_path() : std::filesystem::current_path();
}

dudu::NativeHeaderOptions native_header_options_for_clean_cache(const Options& options) {
    if (options.input.empty() || std::filesystem::is_directory(options.input)) {
        const std::filesystem::path root = options.input.empty() ? "." : options.input;
        return {.config = dudu::parse_project_config(root / "dudu.toml"), .source_dir = root};
    }
    return {.config = config_for_input(options.input),
            .source_dir = source_dir_for_input(options.input)};
}

dudu::ProjectConfig config_for_options(const Options& options) {
    dudu::ProjectConfig config = config_for_input(options.input);
    if (!options.target_name.empty()) {
        config = dudu::apply_project_target(std::move(config), options.target_name);
    }
    return config;
}

std::filesystem::path default_build_output(const dudu::ProjectConfig& config,
                                           const std::filesystem::path& input) {
    if (config.name.empty() && config.build_dir.empty()) {
        return "a.out";
    }
    const std::string name = config.name.empty() ? input.stem().string() : config.name;
    std::filesystem::path filename = name;
    if (config.target_kind == "library") {
        filename = "lib" + name + ".a";
    } else if (config.target_kind == "shared_library") {
        filename = "lib" + name + ".so";
    }
    return config.build_dir.empty() ? filename
                                    : dudu::project_path(config, config.build_dir) / filename;
}

dudu::ModuleAst checked_module(const Options& options, const std::string& source,
                               bool check_bodies) {
    dudu::ModuleAst module = options.input.empty() ? dudu::parse_source(source, options.input)
                                                   : dudu::load_source_tree(options.input);
    const dudu::ProjectConfig config = config_for_options(options);
    module.build_values = config.build_values;
    module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
    module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
    module.target_mode_explicit = config.target_mode_explicit;
    for (const auto& [name, value] : options.build_values) {
        module.build_values[name] = value;
    }
    const std::filesystem::path source_dir = source_dir_for_input(options.input);
    dudu::merge_native_header_types(module, {.config = config, .source_dir = source_dir});
    if (options.emit_modules) {
        dudu::analyze_module_tree(module, {.check_bodies = check_bodies});
    } else {
        dudu::analyze_module(module, {.check_bodies = check_bodies});
    }
    return module;
}

void check_source_file(Options options, const std::filesystem::path& path) {
    options.input = path;
    (void)checked_module(options, read_text_file(path), true);
}

bool check_source_path(const Options& options) {
    if (!std::filesystem::is_directory(options.input)) {
        check_source_file(options, options.input);
        return true;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(options.input)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".dd") {
            continue;
        }
        check_source_file(options, entry.path());
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string executable =
            argc > 0 ? std::filesystem::path(argv[0]).stem().string() : "duc";
        const bool project_driver = executable == "dudu";
        const Options options = resolve_project_input(parse_options(argc, argv, project_driver));
        if (options.lsp) {
            return dudu::run_language_server(std::cin, std::cout, std::cerr);
        }
        if (options.init_project) {
            dudu::init_project(options.input.empty() ? std::filesystem::path(".") : options.input);
            return 0;
        }
        if (options.new_project) {
            if (options.input.empty()) {
                fail("dudu new requires a project name");
            }
            dudu::new_project(options.input);
            return 0;
        }
        if (options.bench) {
            return run_project_benchmarks(options);
        }
        if (options.clean) {
            const std::filesystem::path cleaned = dudu::clean_project(
                options.input.empty() ? std::filesystem::path(".") : options.input);
            std::cerr << "clean " << cleaned.string() << '\n';
            return 0;
        }
        if (options.clean_cache) {
            const std::filesystem::path cleaned =
                dudu::clean_native_header_cache(native_header_options_for_clean_cache(options));
            std::cerr << "clean-cache " << cleaned.string() << '\n';
            return 0;
        }
        if (options.test) {
            return dudu::run_project_tests({.input = options.input,
                                            .output = options.output,
                                            .build_values = options.build_values,
                                            .dudu_executable = argv[0],
                                            .target_name = options.target_name,
                                            .test_filter = options.test_filter,
                                            .no_capture = options.no_capture,
                                            .project_driver = options.project_driver,
                                            .verbose = options.verbose});
        }
        if (options.format) {
            if (options.check) {
                return dudu::check_formatted_path(options.input) ? 0 : 1;
            }
            dudu::format_path(options.input, options.output, std::cout);
            return 0;
        }
        if (options.check) {
            (void)check_source_path(options);
            return 0;
        }
        if (options.cmake) {
            const dudu::ProjectConfig config = config_for_options(options);
            dudu::print_project_step(options.project_driver, "cmake",
                                     options.output.value_or("CMakeLists.txt"));
            write_text_output(options.output, dudu::emit_cmake_project(config, options.input));
            return 0;
        }
        const std::string source = read_text_file(options.input);
        if (options.build) {
            const dudu::ProjectConfig config = config_for_options(options);
            if (config.build_backend == "cmake") {
                const std::filesystem::path root = dudu::default_cmake_backend_root(config);
                dudu::print_project_step(options.project_driver, "cmake",
                                         root / "source" / "CMakeLists.txt");
                dudu::print_project_step(options.project_driver, "build", root / "build");
                (void)build_cmake_backend(options, config, argv[0]);
                return 0;
            }
            const std::filesystem::path output =
                options.output.value_or(default_build_output(config, options.input));
            dudu::print_project_step(options.project_driver, "emit", output.string() + ".cpp");
            dudu::print_project_step(options.project_driver, "build", output);
            (void)dudu::build_executable(
                {.output = output, .config = config, .verbose = options.verbose},
                dudu::emit_cpp_source(checked_module(options, source, true)));
            return 0;
        }
        if (options.run) {
            const dudu::ProjectConfig config = config_for_options(options);
            if (config.target_kind != "executable") {
                fail("cannot run target kind: " + config.target_kind);
            }
            if (config.build_backend == "cmake") {
                const std::filesystem::path root = dudu::default_cmake_backend_root(config);
                dudu::print_project_step(options.project_driver, "cmake",
                                         root / "source" / "CMakeLists.txt");
                dudu::print_project_step(options.project_driver, "build", root / "build");
                const std::filesystem::path bin = build_cmake_backend(options, config, argv[0]);
                dudu::print_project_step(options.project_driver, "run", bin);
                return std::system(dudu::shell_quote_path(bin).c_str()) == 0 ? 0 : 1;
            }
            const std::filesystem::path output =
                options.output.value_or(default_build_output(config, options.input));
            dudu::print_project_step(options.project_driver, "emit", output.string() + ".cpp");
            dudu::print_project_step(options.project_driver, "build", output);
            const std::filesystem::path bin = dudu::build_executable(
                {.output = output, .config = config, .verbose = options.verbose},
                dudu::emit_cpp_source(checked_module(options, source, true)));
            const std::filesystem::path command = bin.is_relative() && bin.parent_path().empty()
                                                      ? std::filesystem::path(".") / bin
                                                      : bin;
            dudu::print_project_step(options.project_driver, "run", command);
            return std::system(dudu::shell_quote_path(command).c_str()) == 0 ? 0 : 1;
        }
        if (options.header_output.has_value()) {
            write_text_output(options.header_output,
                              dudu::emit_cpp_header(checked_module(options, source, false)));
            return 0;
        }
        if (options.c_header_output.has_value()) {
            write_text_output(options.c_header_output,
                              dudu::emit_c_header(checked_module(options, source, false)));
            return 0;
        }
        if (options.emit_modules) {
            if (!options.output.has_value()) {
                fail("emit-modules requires -o <directory>");
            }
            dudu::write_cpp_module_artifacts(*options.output, checked_module(options, source, true));
            return 0;
        }
        if (options.emit_cpp) {
            write_text_output(options.output,
                              dudu::emit_cpp_source(checked_module(options, source, true)));
            return 0;
        }
        write_text_output(std::nullopt,
                          dudu::emit_cpp_source(checked_module(options, source, true)));
    } catch (const std::exception& error) {
        std::cerr << "dudu: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
