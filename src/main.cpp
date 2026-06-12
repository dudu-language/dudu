#include "dudu/cpp_emit.hpp"
#include "dudu/format.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> header_output;
    std::map<std::string, std::string> build_values;
    bool build = false;
    bool check = false;
    bool emit_cpp = false;
    bool format = false;
    bool run = false;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void print_usage() {
    std::cout << "usage: duc build <input.dd> [-o output]\n"
                 "       duc check <input.dd>\n"
                 "       duc emit <input.dd> [-o output.cpp]\n"
                 "       duc fmt <input.dd> [-o output.dd]\n"
                 "       duc run <input.dd> [-o output]\n"
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

Options parse_options(int argc, char** argv) {
    Options options;
    int first_arg = 1;
    if (argc > 1 && std::string(argv[1]) == "build") {
        options.build = true;
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
    }

    for (int i = first_arg; i < argc; ++i) {
        const std::string arg = argv[i];
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
    if (options.input.empty()) {
        fail("missing input file");
    }
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

std::filesystem::path build_executable(const Options& options, const std::string& cpp) {
    const std::filesystem::path output = options.output.value_or("a.out");
    std::filesystem::create_directories(output.parent_path().empty() ? "." : output.parent_path());
    const std::filesystem::path cpp_path = output.string() + ".cpp";
    write_text_output(cpp_path, cpp);

    const char* env_cxx = std::getenv("CXX");
    const std::string cxx = env_cxx == nullptr ? "c++" : env_cxx;
    const std::string command =
        cxx + " -std=c++20 " + shell_quote(cpp_path) + " -o " + shell_quote(output);
    if (std::system(command.c_str()) != 0) {
        fail("C++ build failed");
    }
    return output;
}

dudu::ModuleAst checked_module(const Options& options, const std::string& source,
                               bool check_bodies) {
    dudu::ModuleAst module = options.input.empty() ? dudu::parse_source(source, options.input)
                                                   : dudu::load_source_tree(options.input);
    module.build_values = options.build_values;
    dudu::analyze_module(module, {.check_bodies = check_bodies});
    return module;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const std::string source = read_text_file(options.input);
        if (options.format) {
            write_text_output(options.output, dudu::format_source(source));
            return 0;
        }
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
        if (options.check) {
            (void)checked_module(options, source, false);
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
