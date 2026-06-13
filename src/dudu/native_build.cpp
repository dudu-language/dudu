#include "dudu/native_build.hpp"

#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string native_lib_flag(const std::string& lib) {
    return lib.empty() || lib.front() == '-' ? lib : "-l" + lib;
}

void append_source_files(std::string& command, const ProjectConfig& config) {
    for (const std::string& source : config.cpp_sources) {
        command += " " + shell_quote_arg(source);
    }
    for (const std::string& source : config.c_sources) {
        command += " " + shell_quote_arg(source);
    }
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

void append_target_mode_flags(std::string& common_flags, const std::string& mode) {
    if (mode != "freestanding" && mode != "embedded") {
        return;
    }
    for (const char* flag : {"-ffreestanding", "-fno-exceptions", "-fno-rtti"}) {
        common_flags += " " + shell_quote_arg(flag);
    }
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

std::string read_text_file(const std::filesystem::path& path);

bool write_text_if_changed(const std::filesystem::path& path, const std::string& text) {
    if (read_text_file(path) == text) {
        return false;
    }
    write_text_output(path, text);
    return true;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool file_newer_or_same(const std::filesystem::path& newer, const std::filesystem::path& older) {
    std::error_code error;
    if (!std::filesystem::exists(newer, error) || error ||
        !std::filesystem::exists(older, error) || error) {
        return false;
    }
    const auto newer_time = std::filesystem::last_write_time(newer, error);
    if (error) {
        return false;
    }
    const auto older_time = std::filesystem::last_write_time(older, error);
    if (error) {
        return false;
    }
    return newer_time >= older_time;
}

bool native_sources_older_than(const std::filesystem::path& output, const ProjectConfig& config) {
    for (const std::string& source : config.cpp_sources) {
        if (!file_newer_or_same(output, source)) {
            return false;
        }
    }
    for (const std::string& source : config.c_sources) {
        if (!file_newer_or_same(output, source)) {
            return false;
        }
    }
    return true;
}

bool build_is_up_to_date(const std::filesystem::path& output, const std::filesystem::path& cpp_path,
                         const ProjectConfig& config, const std::string& command,
                         bool cpp_changed) {
    if (cpp_changed || !file_newer_or_same(output, cpp_path) ||
        !native_sources_older_than(output, config)) {
        return false;
    }
    return read_text_file(output.string() + ".command") == command;
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

int run_shell_command(const std::string& command, const std::filesystem::path& log_path) {
    return std::system((command + " > " + shell_quote_path(log_path) + " 2>&1").c_str());
}

std::optional<int> first_generated_error_line(const std::string& output,
                                              const std::filesystem::path& cpp_path) {
    std::istringstream lines(output);
    std::string line;
    const std::string path = cpp_path.string() + ":";
    while (std::getline(lines, line)) {
        size_t pos = line.find(path);
        if (pos == std::string::npos) {
            continue;
        }
        pos += path.size();
        if (pos >= line.size() || std::isdigit(static_cast<unsigned char>(line[pos])) == 0) {
            continue;
        }
        int value = 0;
        while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
            value = value * 10 + line[pos] - '0';
            ++pos;
        }
        return value;
    }
    return std::nullopt;
}

std::string dudu_source_for_generated_line(const std::filesystem::path& cpp_path, int line_number) {
    std::ifstream in(cpp_path);
    if (!in) {
        return {};
    }
    std::string line;
    std::string last_source;
    for (int current = 1; current <= line_number && std::getline(in, line); ++current) {
        const std::string marker = "// dudu: ";
        const size_t pos = line.find(marker);
        if (pos != std::string::npos) {
            last_source = line.substr(pos + marker.size());
        }
    }
    trim_ascii_whitespace(last_source);
    return last_source;
}

std::optional<SourceLocation> parse_dudu_marker(const std::string& marker) {
    const size_t second_colon = marker.rfind(':');
    if (second_colon == std::string::npos || second_colon == 0) {
        return std::nullopt;
    }
    const size_t first_colon = marker.rfind(':', second_colon - 1);
    if (first_colon == std::string::npos || first_colon == 0) {
        return std::nullopt;
    }
    SourceLocation location;
    location.file = marker.substr(0, first_colon);
    location.line =
        std::atoi(marker.substr(first_colon + 1, second_colon - first_colon - 1).c_str());
    location.column = std::atoi(marker.substr(second_colon + 1).c_str());
    return location.line > 0 && location.column > 0 ? std::optional<SourceLocation>{location}
                                                    : std::nullopt;
}

std::string source_excerpt(const SourceLocation& location) {
    std::ifstream in(location.file);
    if (!in) {
        return {};
    }
    std::string line;
    for (int current = 1; current <= location.line && std::getline(in, line); ++current) {
    }
    if (line.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "    " << line << '\n'
        << "    " << std::string(static_cast<size_t>(std::max(location.column - 1, 0)), ' ') << '^';
    return out.str();
}

std::string native_failure_message(std::string label, const std::filesystem::path& source,
                                   const std::string& command,
                                   const std::filesystem::path& log_path) {
    const std::string output = read_text_file(log_path);
    std::string message = std::move(label) + " failed\nsource: " + source.string();
    if (const std::optional<int> line = first_generated_error_line(output, source)) {
        const std::string dudu_source = dudu_source_for_generated_line(source, *line);
        if (!dudu_source.empty()) {
            message += "\ndudu source: " + dudu_source;
            if (const std::optional<SourceLocation> location = parse_dudu_marker(dudu_source)) {
                const std::string excerpt = source_excerpt(*location);
                if (!excerpt.empty()) {
                    message += "\n" + excerpt;
                }
            }
        }
    }
    message += "\ncommand: " + command;
    if (!output.empty()) {
        message += "\ncompiler output:\n" + output;
    }
    return message;
}

std::filesystem::path build_executable(const NativeBuildOptions& options, const std::string& cpp) {
    const std::filesystem::path output = options.output.empty() ? "a.out" : options.output;
    std::filesystem::create_directories(output.parent_path().empty() ? "." : output.parent_path());
    const std::filesystem::path cpp_path = output.string() + ".cpp";
    const std::filesystem::path object_path = output.string() + ".o";
    const bool cpp_changed = write_text_if_changed(cpp_path, cpp);

    const char* env_cxx = std::getenv("CXX");
    const std::string cxx = options.config.compiler.empty() ? (env_cxx == nullptr ? "c++" : env_cxx)
                                                            : options.config.compiler;
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
    append_target_mode_flags(common_flags, options.config.target_mode);
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
        command += shell_quote_path(cpp_path);
        append_source_files(command, options.config);
        command += " -o " + shell_quote_path(output) + common_flags;
    }
    if (options.config.target_kind != "library") {
        for (const std::string& lib : options.config.libs) {
            command += " " + shell_quote_arg(native_lib_flag(lib));
        }
        for (const std::string& flag : options.config.link_flags) {
            command += " " + shell_quote_arg(flag);
        }
    }
    write_compile_commands(output, cpp_path, command);
    if (build_is_up_to_date(output, cpp_path, options.config, command, cpp_changed)) {
        if (options.verbose) {
            std::cerr << "up-to-date " << output.string() << '\n';
        }
        return output;
    }
    if (options.verbose) {
        std::cerr << command << '\n';
    }
    const std::filesystem::path compile_log = output.string() + ".compile.log";
    if (run_shell_command(command, compile_log) != 0) {
        fail(native_failure_message("C++ build", cpp_path, command, compile_log));
    }
    if (options.config.target_kind == "library") {
        const char* env_ar = std::getenv("AR");
        const std::string ar = env_ar == nullptr ? "ar" : env_ar;
        const std::string archive_command =
            ar + " rcs " + shell_quote_path(output) + " " + shell_quote_path(object_path);
        if (options.verbose) {
            std::cerr << archive_command << '\n';
        }
        const std::filesystem::path archive_log = output.string() + ".archive.log";
        if (run_shell_command(archive_command, archive_log) != 0) {
            fail(
                native_failure_message("archive build", object_path, archive_command, archive_log));
        }
    }
    write_text_output(output.string() + ".command", command);
    return output;
}

} // namespace dudu
