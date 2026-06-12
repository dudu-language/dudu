#include "dudu/cpp_emit.hpp"
#include "dudu/format_path.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/sema.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> header_output;
    std::map<std::string, std::string> build_values;
    std::vector<std::string> command_args;
    bool bench = false;
    bool build = false;
    bool check = false;
    bool emit_cpp = false;
    bool format = false;
    bool run = false;
    bool test = false;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void print_usage() {
    std::cout << "usage: duc bench [args...]\n"
                 "       duc build [input.dd] [-o output]\n"
                 "       duc check [input.dd]\n"
                 "       duc emit [input.dd] [-o output.cpp]\n"
                 "       duc fmt <input.dd|dir> [--check] [-o output.dd]\n"
                 "       duc run [input.dd] [-o output]\n"
                 "       duc test\n"
                 "       duc <input.dd> [--check] [--format <path|->] "
                 "[--emit-header <path|->] [--emit-cpp <path|->] [-DNAME=value]\n";
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

Options parse_options(int argc, char** argv) {
    Options options;
    int first_arg = 1;
    if (argc > 1 && std::string(argv[1]) == "build") {
        options.build = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "bench") {
        options.bench = true;
        first_arg = 2;
    } else if (argc > 1 && std::string(argv[1]) == "check") {
        options.check = true;
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
            print_usage();
            std::exit(0);
        }
        if (arg == "--version") {
            print_version();
            std::exit(0);
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
        !options.emit_cpp && !options.run && !options.test) {
        fail("missing input file");
    }
    return options;
}

Options resolve_project_input(Options options) {
    if (options.bench || options.test) {
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

std::string shell_quote(const std::filesystem::path& path) {
    std::string out = "'";
    for (const char c : path.string()) {
        out += c == '\'' ? "'\\''" : std::string(1, c);
    }
    out += "'";
    return out;
}

std::string shell_quote_arg(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        out += c == '\'' ? "'\\''" : std::string(1, c);
    }
    out += "'";
    return out;
}

std::string append_command_args(std::string command, const std::vector<std::string>& args) {
    for (const std::string& arg : args) {
        command += " " + shell_quote_arg(arg);
    }
    return command;
}

std::string native_lib_flag(const std::string& lib) {
    return lib.empty() || lib.front() == '-' ? lib : "-l" + lib;
}

std::filesystem::path build_executable(const Options& options, const std::string& cpp) {
    const std::filesystem::path output = options.output.value_or("a.out");
    std::filesystem::create_directories(output.parent_path().empty() ? "." : output.parent_path());
    const std::filesystem::path cpp_path = output.string() + ".cpp";
    write_text_output(cpp_path, cpp);

    const dudu::ProjectConfig config = dudu::parse_project_config(build_config_path(options.input));
    const char* env_cxx = std::getenv("CXX");
    const std::string cxx = env_cxx == nullptr ? "c++" : env_cxx;
    std::string command = cxx + " -std=" + config.cpp_std + " " + shell_quote(cpp_path) + " -o " +
                          shell_quote(output);
    for (const std::string& include_dir : config.include_dirs) {
        command += " " + shell_quote_arg("-I" + include_dir);
    }
    for (const std::string& lib : config.libs) {
        command += " " + shell_quote_arg(native_lib_flag(lib));
    }
    if (std::system(command.c_str()) != 0) {
        fail("C++ build failed");
    }
    return output;
}

int run_project_tests() {
    std::string command = dudu::parse_project_config("dudu.toml").test_command;
    if (command.empty() && std::filesystem::exists("scripts/test.sh")) {
        command = "./scripts/test.sh";
    }
    if (command.empty()) {
        fail("missing [test] command and scripts/test.sh");
    }
    return std::system(command.c_str()) == 0 ? 0 : 1;
}

int run_project_benchmarks(const Options& options) {
    std::string command = dudu::parse_project_config("dudu.toml").bench_command;
    if (command.empty() && std::filesystem::exists("scripts/bench.sh")) {
        command = "./scripts/bench.sh";
    }
    if (command.empty()) {
        fail("missing [bench] command and scripts/bench.sh");
    }
    return std::system(append_command_args(command, options.command_args).c_str()) == 0 ? 0 : 1;
}

dudu::ModuleAst checked_module(const Options& options, const std::string& source,
                               bool check_bodies) {
    dudu::ModuleAst module = options.input.empty() ? dudu::parse_source(source, options.input)
                                                   : dudu::load_source_tree(options.input);
    module.build_values = dudu::parse_project_config(build_config_path(options.input)).build_values;
    for (const auto& [name, value] : options.build_values) {
        module.build_values[name] = value;
    }
    dudu::analyze_module(module, {.check_bodies = check_bodies});
    return module;
}

void check_source_file(Options options, const std::filesystem::path& path) {
    options.input = path;
    (void)checked_module(options, read_text_file(path), false);
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
        const Options options = resolve_project_input(parse_options(argc, argv));
        if (options.bench) {
            return run_project_benchmarks(options);
        }
        if (options.test) {
            return run_project_tests();
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
        const std::string source = read_text_file(options.input);
        if (options.build) {
            (void)build_executable(options,
                                   dudu::emit_cpp_source(checked_module(options, source, true)));
            return 0;
        }
        if (options.run) {
            const std::filesystem::path bin = build_executable(
                options, dudu::emit_cpp_source(checked_module(options, source, true)));
            const std::filesystem::path command = bin.is_relative() && bin.parent_path().empty()
                                                      ? std::filesystem::path(".") / bin
                                                      : bin;
            return std::system(shell_quote(command).c_str()) == 0 ? 0 : 1;
        }
        if (options.header_output.has_value()) {
            write_text_output(options.header_output,
                              dudu::emit_cpp_header(checked_module(options, source, false)));
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
