#include "dudu/cpp_emit.hpp"
#include "dudu/format.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

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

struct ProjectConfig {
    std::filesystem::path main;
    std::map<std::string, std::string> build_values;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void print_usage() {
    std::cout << "usage: duc build [input.dd] [-o output]\n"
                 "       duc check [input.dd]\n"
                 "       duc emit [input.dd] [-o output.cpp]\n"
                 "       duc fmt <input.dd|dir> [--check] [-o output.dd]\n"
                 "       duc run [input.dd] [-o output]\n"
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

std::string trim_copy(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::string strip_comment(std::string line) {
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

ProjectConfig parse_project_config(const std::filesystem::path& path) {
    ProjectConfig config;
    std::ifstream file(path);
    if (!file) {
        return config;
    }
    bool in_build = false;
    std::string line;
    while (std::getline(file, line)) {
        line = trim_copy(strip_comment(std::move(line)));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            in_build = trim_copy(std::string_view(line).substr(1, line.size() - 2)) == "build";
            continue;
        }
        const size_t equal = line.find('=');
        if (equal == std::string::npos) {
            if (in_build) {
                fail(path.string() + ": invalid [build] entry: " + line);
            }
            fail(path.string() + ": invalid entry: " + line);
        }
        const std::string name = trim_copy(std::string_view(line).substr(0, equal));
        std::string value = trim_copy(std::string_view(line).substr(equal + 1));
        if (name.empty() || value.empty()) {
            if (in_build) {
                fail(path.string() + ": invalid [build] entry: " + line);
            }
            fail(path.string() + ": invalid entry: " + line);
        }
        if (!in_build && name == "main") {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            config.main = value;
            continue;
        }
        if (!in_build) {
            continue;
        }
        config.build_values[name] = value;
    }
    return config;
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
    if (options.input.empty() && !options.build && !options.check && !options.emit_cpp &&
        !options.run) {
        fail("missing input file");
    }
    return options;
}

Options resolve_project_input(Options options) {
    if (!options.input.empty()) {
        return options;
    }
    const ProjectConfig config = parse_project_config("dudu.toml");
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

bool check_formatted_file(const std::filesystem::path& path) {
    const std::string source = read_text_file(path);
    if (dudu::format_source(source) == source) {
        return true;
    }
    std::cerr << path.string() << ": would reformat\n";
    return false;
}

bool check_formatted_path(const std::filesystem::path& path) {
    if (!std::filesystem::is_directory(path)) {
        return check_formatted_file(path);
    }
    bool ok = true;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(path)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".dd") {
            continue;
        }
        ok = check_formatted_file(entry.path()) && ok;
    }
    return ok;
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
    module.build_values = parse_project_config(build_config_path(options.input)).build_values;
    for (const auto& [name, value] : options.build_values) {
        module.build_values[name] = value;
    }
    dudu::analyze_module(module, {.check_bodies = check_bodies});
    return module;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = resolve_project_input(parse_options(argc, argv));
        if (options.format) {
            if (options.check) {
                return check_formatted_path(options.input) ? 0 : 1;
            }
            const std::string source = read_text_file(options.input);
            write_text_output(options.output, dudu::format_source(source));
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
