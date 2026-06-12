#include "dudu/native_build.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string native_lib_flag(const std::string& lib) {
    return lib.empty() || lib.front() == '-' ? lib : "-l" + lib;
}

void trim_ascii_whitespace(std::string& value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    value.erase(0, start);
}

std::string capture_command(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        fail("could not run command: " + command);
    }
    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    if (pclose(pipe) != 0) {
        fail("command failed: " + command);
    }
    trim_ascii_whitespace(output);
    return output;
}

std::string pkg_config_flags(const std::vector<std::string>& packages) {
    if (packages.empty()) {
        return {};
    }
    const char* env_pkg_config = std::getenv("PKG_CONFIG");
    std::string command =
        shell_quote_arg(env_pkg_config == nullptr ? "pkg-config" : std::string(env_pkg_config)) +
        " --cflags --libs";
    for (const std::string& package : packages) {
        command += " " + shell_quote_arg(package);
    }
    return capture_command(command);
}

std::string json_escape(const std::string& value) {
    std::string out;
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

void write_text_output(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (!out) {
        fail("could not open output " + path.string());
    }
    out << text;
}

void write_compile_commands(const std::filesystem::path& output,
                            const std::filesystem::path& cpp_path, const std::string& command) {
    const std::filesystem::path dir = output.parent_path().empty() ? "." : output.parent_path();
    std::ofstream out(dir / "compile_commands.json");
    if (!out) {
        fail("could not write compile_commands.json");
    }
    out << "[\n"
        << "  {\n"
        << "    \"directory\": \"" << json_escape(std::filesystem::current_path().string())
        << "\",\n"
        << "    \"command\": \"" << json_escape(command) << "\",\n"
        << "    \"file\": \"" << json_escape(cpp_path.string()) << "\"\n"
        << "  }\n"
        << "]\n";
}

} // namespace

std::string shell_quote_arg(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        out += c == '\'' ? "'\\''" : std::string(1, c);
    }
    out += "'";
    return out;
}

std::string shell_quote_path(const std::filesystem::path& path) {
    return shell_quote_arg(path.string());
}

std::string append_command_args(std::string command, const std::vector<std::string>& args) {
    for (const std::string& arg : args) {
        command += " " + shell_quote_arg(arg);
    }
    return command;
}

std::filesystem::path build_executable(const NativeBuildOptions& options, const std::string& cpp) {
    const std::filesystem::path output = options.output.empty() ? "a.out" : options.output;
    std::filesystem::create_directories(output.parent_path().empty() ? "." : output.parent_path());
    const std::filesystem::path cpp_path = output.string() + ".cpp";
    const std::filesystem::path object_path = output.string() + ".o";
    write_text_output(cpp_path, cpp);

    const char* env_cxx = std::getenv("CXX");
    const std::string cxx = env_cxx == nullptr ? "c++" : env_cxx;
    std::string common_flags;
    for (const std::string& include_dir : options.config.include_dirs) {
        common_flags += " " + shell_quote_arg("-I" + include_dir);
    }
    for (const std::string& define : options.config.defines) {
        common_flags += " " + shell_quote_arg("-D" + define);
    }
    for (const std::string& lib_dir : options.config.lib_dirs) {
        common_flags += " " + shell_quote_arg("-L" + lib_dir);
    }
    for (const std::string& flag : options.config.flags) {
        common_flags += " " + shell_quote_arg(flag);
    }
    const std::string package_flags = pkg_config_flags(options.config.pkg_config_packages);
    if (!package_flags.empty()) {
        common_flags += " " + package_flags;
    }

    std::string command;
    if (options.config.target_kind == "library") {
        command = cxx + " -std=" + options.config.cpp_std + " -c " + shell_quote_path(cpp_path) +
                  " -o " + shell_quote_path(object_path) + common_flags;
    } else {
        command = cxx + " -std=" + options.config.cpp_std + " ";
        if (options.config.target_kind == "shared_library") {
            command += "-fPIC -shared ";
        }
        command += shell_quote_path(cpp_path) + " -o " + shell_quote_path(output) + common_flags;
    }
    if (options.config.target_kind != "library") {
        for (const std::string& lib : options.config.libs) {
            command += " " + shell_quote_arg(native_lib_flag(lib));
        }
    }
    write_compile_commands(output, cpp_path, command);
    if (options.verbose) {
        std::cerr << command << '\n';
    }
    if (std::system(command.c_str()) != 0) {
        fail("C++ build failed\nsource: " + cpp_path.string() + "\ncommand: " + command);
    }
    if (options.config.target_kind == "library") {
        const char* env_ar = std::getenv("AR");
        const std::string ar = env_ar == nullptr ? "ar" : env_ar;
        const std::string archive_command =
            ar + " rcs " + shell_quote_path(output) + " " + shell_quote_path(object_path);
        if (options.verbose) {
            std::cerr << archive_command << '\n';
        }
        if (std::system(archive_command.c_str()) != 0) {
            fail("archive build failed\nsource: " + object_path.string() +
                 "\ncommand: " + archive_command);
        }
    }
    return output;
}

} // namespace dudu
