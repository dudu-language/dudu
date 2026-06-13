#include "dudu/cpp_emit.hpp"
#include "dudu/cmake_emit.hpp"
#include "dudu/format_path.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/project_driver.hpp"
#include "dudu/sema.hpp"

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
    std::vector<std::string> command_args;
    std::string test_filter;
    bool bench = false;
    bool build = false;
    bool check = false;
    bool cmake = false;
    bool emit_cpp = false;
    bool format = false;
    bool init_project = false;
    bool new_project = false;
    bool run = false;
    bool test = false;
    bool verbose = false;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void print_usage(bool project_driver = false) {
    if (project_driver) {
        std::cout << "usage: dudu init\n"
                     "       dudu init [path]\n"
                     "       dudu new <name>\n"
                     "       dudu run [input.dd] [-o output]\n"
                     "       dudu build [input.dd] [-o output]\n"
                     "       dudu check [input.dd|dir]\n"
                     "       dudu fmt <input.dd|dir> [--check]\n"
                     "       dudu test [input.dd|filter] [--filter text]\n"
                     "       dudu cmake [input.dd] [-o CMakeLists.txt]\n";
        return;
    }
    std::cout << "usage: duc bench [args...]\n"
                 "       duc build [input.dd] [-o output]\n"
                 "       duc check [input.dd]\n"
                 "       duc cmake [input.dd] [-o CMakeLists.txt]\n"
                 "       duc emit [input.dd] [-o output.cpp]\n"
                 "       duc fmt <input.dd|dir> [--check] [-o output.dd]\n"
                 "       duc run [input.dd] [-o output]\n"
                 "       duc test [input.dd|filter] [--filter text]\n"
                 "       duc <input.dd> [--check] [--format <path|->] "
                 "[--emit-header <path|->] [--emit-c-header <path|->] "
                 "[--emit-cpp <path|->] [-DNAME=value] [--verbose]\n";
}

void print_version() {
    std::cout << "duc 0.1.0\n";
}

void add_build_value(Options& options, const std::string& define) {
    const size_t equal = define.find('=');
    if (equal == std::string::npos || equal == 0) {
        fail("-D requires NAME=value");
    }
    options.build_values[define.substr(0, equal)] = define.substr(equal + 1);
}

std::filesystem::path build_config_path(const std::filesystem::path& input) {
    const std::filesystem::path beside_input = input.parent_path() / "dudu.toml";
    if (!input.parent_path().empty() && std::filesystem::exists(beside_input)) {
        return beside_input;
    }
    return "dudu.toml";
}

Options parse_options(int argc, char** argv, bool project_driver) {
    Options options;
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
    } else if (argc > 1 && std::string(argv[1]) == "cmake") {
        options.cmake = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "emit") {
        options.emit_cpp = true;
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
            options.command_args.push_back(arg);
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            print_usage(project_driver);
            std::exit(0);
        }
        if (arg == "--version") {
            print_version();
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
        !options.cmake && !options.emit_cpp && !options.init_project && !options.new_project &&
        !options.run && !options.test) {
        fail("missing input file");
    }
    return options;
}

Options resolve_project_input(Options options) {
    if (options.bench || options.init_project || options.new_project || options.test) {
        return options;
    }
    if (!options.input.empty()) {
        return options;
    }
    const dudu::ProjectConfig config = dudu::parse_project_config("dudu.toml");
    if (config.main.empty()) {
        fail("missing input file and dudu.toml main");
    }
    options.input = config.main;
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

int run_project_benchmarks(const Options& options) {
    std::string command = dudu::parse_project_config("dudu.toml").bench_command;
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
    return config.build_dir.empty() ? filename : config.build_dir / filename;
}

dudu::ModuleAst checked_module(const Options& options, const std::string& source,
                               bool check_bodies) {
    dudu::ModuleAst module = options.input.empty() ? dudu::parse_source(source, options.input)
                                                   : dudu::load_source_tree(options.input);
    const dudu::ProjectConfig config = config_for_input(options.input);
    module.build_values = config.build_values;
    module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
    module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
    module.target_mode_explicit = config.target_mode_explicit;
    for (const auto& [name, value] : options.build_values) {
        module.build_values[name] = value;
    }
    const std::filesystem::path source_dir =
        options.input.empty() ? std::filesystem::current_path() : options.input.parent_path();
    dudu::merge_native_header_types(module, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(module, {.check_bodies = check_bodies});
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

bool looks_like_test_input(const std::filesystem::path& value) {
    return value.extension() == ".dd" || std::filesystem::exists(value);
}

int run_delegated_project_tests() {
    std::string command = dudu::parse_project_config("dudu.toml").test_command;
    if (command.empty() && std::filesystem::exists("scripts/test.sh")) {
        command = "./scripts/test.sh";
    }
    if (command.empty()) {
        fail("missing test entry and no [test] command or scripts/test.sh");
    }
    return std::system(command.c_str()) == 0 ? 0 : 1;
}

int run_project_tests(Options options) {
    if (!options.input.empty() && !looks_like_test_input(options.input)) {
        if (options.test_filter.empty()) {
            options.test_filter = options.input.string();
        }
        options.input.clear();
    }
    if (options.input.empty()) {
        options.input = dudu::parse_project_config("dudu.toml").main;
    }
    if (options.input.empty()) {
        return run_delegated_project_tests();
    }
    const std::string source = read_text_file(options.input);
    const dudu::ProjectConfig config = config_for_input(options.input);
    const std::filesystem::path output =
        options.output.value_or(config.build_dir.empty() ? std::filesystem::path("build/dudu_tests")
                                                         : config.build_dir / "dudu_tests");
    const std::filesystem::path bin = dudu::build_executable(
        {.output = output, .config = config, .verbose = options.verbose},
        dudu::emit_cpp_test_source(checked_module(options, source, true), options.test_filter));
    const std::filesystem::path command = bin.is_relative() && bin.parent_path().empty()
                                              ? std::filesystem::path(".") / bin
                                              : bin;
    return std::system(dudu::shell_quote_path(command).c_str()) == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string executable =
            argc > 0 ? std::filesystem::path(argv[0]).stem().string() : "duc";
        const bool project_driver = executable == "dudu";
        const Options options = resolve_project_input(parse_options(argc, argv, project_driver));
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
        if (options.test) {
            return run_project_tests(options);
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
            const dudu::ProjectConfig config = config_for_input(options.input);
            write_text_output(options.output, dudu::emit_cmake_project(config, options.input));
            return 0;
        }
        const std::string source = read_text_file(options.input);
        if (options.build) {
            const dudu::ProjectConfig config =
                dudu::parse_project_config(build_config_path(options.input));
            (void)dudu::build_executable(
                {.output = options.output.value_or(default_build_output(config, options.input)),
                 .config = config,
                 .verbose = options.verbose},
                dudu::emit_cpp_source(checked_module(options, source, true)));
            return 0;
        }
        if (options.run) {
            const dudu::ProjectConfig config = config_for_input(options.input);
            if (config.target_kind != "executable") {
                fail("cannot run target kind: " + config.target_kind);
            }
            const std::filesystem::path bin = dudu::build_executable(
                {.output = options.output.value_or(default_build_output(config, options.input)),
                 .config = config,
                 .verbose = options.verbose},
                dudu::emit_cpp_source(checked_module(options, source, true)));
            const std::filesystem::path command = bin.is_relative() && bin.parent_path().empty()
                                                      ? std::filesystem::path(".") / bin
                                                      : bin;
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
