#include "dudu/project_config.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& message,
                       const std::string& line) {
    throw std::runtime_error(path.string() + ": " + message + ": " + line);
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
        } else if (c == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string unquote(std::string value) {
    value = trim_copy(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::vector<std::string> parse_string_array(const std::filesystem::path& path,
                                            const std::string& line, std::string value) {
    value = trim_copy(std::move(value));
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        fail(path, "invalid array entry", line);
    }
    std::vector<std::string> out;
    value = value.substr(1, value.size() - 2);
    size_t start = 0;
    while (start < value.size()) {
        const size_t comma = value.find(',', start);
        const std::string item =
            trim_copy(value.substr(start, comma == std::string::npos ? comma : comma - start));
        if (!item.empty()) {
            out.push_back(unquote(item));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

void validate_one_of(const std::filesystem::path& path, const std::string& line,
                     const std::string& field, const std::string& value,
                     const std::set<std::string>& allowed) {
    if (!allowed.contains(value)) {
        fail(path, "invalid [target] " + field, line);
    }
}

} // namespace

ProjectConfig parse_project_config(const std::filesystem::path& path) {
    ProjectConfig config;
    std::ifstream file(path);
    if (!file) {
        return config;
    }
    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        line = trim_copy(strip_comment(std::move(line)));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim_copy(std::string_view(line).substr(1, line.size() - 2));
            continue;
        }
        const size_t equal = line.find('=');
        if (equal == std::string::npos) {
            fail(path, section == "build" ? "invalid [build] entry" : "invalid entry", line);
        }
        const std::string name = trim_copy(std::string_view(line).substr(0, equal));
        std::string value = trim_copy(std::string_view(line).substr(equal + 1));
        if (name.empty() || value.empty()) {
            fail(path, section == "build" ? "invalid [build] entry" : "invalid entry", line);
        }
        if (section.empty() && name == "main") {
            config.main = unquote(value);
        } else if (section.empty() && name == "cpp_std") {
            config.cpp_std = unquote(value);
        } else if (section == "target" && name == "kind") {
            config.target_kind = unquote(value);
            validate_one_of(path, line, "kind", config.target_kind,
                            {"executable", "library", "shared_library"});
        } else if (section == "target" && name == "mode") {
            config.target_mode = unquote(value);
            config.target_mode_explicit = true;
            validate_one_of(path, line, "mode", config.target_mode,
                            {"hosted", "freestanding", "embedded", "cuda", "shader"});
        } else if (section == "bench" && name == "command") {
            config.bench_command = unquote(value);
        } else if (section == "test" && name == "command") {
            config.test_command = unquote(value);
        } else if (section == "cc" && name == "compiler") {
            config.compiler = unquote(value);
        } else if (section == "cc" && name == "include_dirs") {
            config.include_dirs = parse_string_array(path, line, value);
        } else if (section == "cc" && name == "lib_dirs") {
            config.lib_dirs = parse_string_array(path, line, value);
        } else if (section == "cc" && name == "libs") {
            config.libs = parse_string_array(path, line, value);
        } else if (section == "cc" && name == "defines") {
            config.defines = parse_string_array(path, line, value);
        } else if (section == "cc" && name == "flags") {
            config.flags = parse_string_array(path, line, value);
        } else if (section == "pkg_config" && name == "packages") {
            config.pkg_config_packages = parse_string_array(path, line, value);
        } else if (section == "build") {
            config.build_values[name] = value;
        } else {
            fail(path,
                 section.empty() ? "unknown top-level entry" : "unknown [" + section + "] entry",
                 line);
        }
    }
    return config;
}

} // namespace dudu
