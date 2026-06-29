#include "dudu/native/native_build.hpp"

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

std::string project_shell_command(const ProjectConfig& config, const std::string& command) {
    const std::filesystem::path root =
        config.project_dir.empty() ? std::filesystem::current_path() : config.project_dir;
    return "cd " + shell_quote_path(root) + " && " + command;
}

int run_shell_command(const std::string& command, const std::filesystem::path& log_path) {
    return std::system((command + " > " + shell_quote_path(log_path) + " 2>&1").c_str());
}

int run_shell_command_streaming(const std::string& command, const std::filesystem::path& log_path) {
    std::filesystem::create_directories(log_path.parent_path().empty() ? "."
                                                                       : log_path.parent_path());
    std::ofstream log(log_path);
    if (!log) {
        fail("could not open log " + log_path.string());
    }
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        fail("could not run command: " + command);
    }
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::cerr << buffer;
        log << buffer;
    }
    return pclose(pipe);
}

} // namespace dudu
