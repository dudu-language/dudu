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

bool parse_bool_value(const std::filesystem::path& path, const std::string& line,
                      const std::string& value) {
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    fail(path, "expected boolean", line);
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
    char quote = '\0';
    bool escaped = false;
    for (size_t cursor = 0; cursor <= value.size(); ++cursor) {
        const char c = cursor < value.size() ? value[cursor] : ',';
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
        if (c != ',') {
            continue;
        }
        const std::string item = trim_copy(value.substr(start, cursor - start));
        if (!item.empty()) {
            out.push_back(unquote(item));
        }
        start = cursor + 1;
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

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

} // namespace

ProjectConfig apply_project_target(ProjectConfig config, const std::string& target_name) {
    const auto found = config.targets.find(target_name);
    if (found == config.targets.end()) {
        return config;
    }
    config.name = target_name;
    if (!found->second.main.empty()) {
        config.main = found->second.main;
    }
    if (!found->second.target_kind.empty()) {
        config.target_kind = found->second.target_kind;
    }
    if (!found->second.target_mode.empty()) {
        config.target_mode = found->second.target_mode;
        config.target_mode_explicit = found->second.target_mode_explicit;
    }
    return config;
}

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
        if (starts_with(section, "targets.")) {
            const std::string target_name = section.substr(8);
            if (target_name.empty()) {
                fail(path, "invalid target section", line);
            }
            ProjectTarget& target = config.targets[target_name];
            if (name == "entry" || name == "main") {
                target.main = unquote(value);
            } else if (name == "kind") {
                target.target_kind = unquote(value);
                validate_one_of(path, line, "kind", target.target_kind,
                                {"executable", "library", "shared_library"});
            } else if (name == "mode") {
                target.target_mode = unquote(value);
                target.target_mode_explicit = true;
                validate_one_of(path, line, "mode", target.target_mode,
                                {"hosted", "freestanding", "embedded", "cuda", "shader"});
            } else {
                fail(path, "unknown [" + section + "] entry", line);
            }
        } else if (section.empty() && name == "name") {
            config.name = unquote(value);
        } else if (section.empty() && (name == "main" || name == "entry")) {
            config.main = unquote(value);
        } else if (section.empty() && name == "build_dir") {
            config.build_dir = unquote(value);
        } else if (section.empty() && name == "cpp_std") {
            config.cpp_std = unquote(value);
        } else if (section == "cxx" && name == "standard") {
            config.cpp_std = unquote(value);
        } else if (section == "cxx" && name == "compiler") {
            config.compiler = unquote(value);
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
        } else if (section == "include" && name == "paths") {
            config.include_dirs = parse_string_array(path, line, value);
        } else if (section == "sources" && name == "cpp") {
            config.cpp_sources = parse_string_array(path, line, value);
        } else if (section == "sources" && name == "c") {
            config.c_sources = parse_string_array(path, line, value);
        } else if (section == "cc" && name == "lib_dirs") {
            config.lib_dirs = parse_string_array(path, line, value);
        } else if (section == "link" && name == "paths") {
            config.lib_dirs = parse_string_array(path, line, value);
        } else if (section == "cc" && name == "libs") {
            config.libs = parse_string_array(path, line, value);
        } else if (section == "link" && name == "libs") {
            config.libs = parse_string_array(path, line, value);
        } else if (section == "cc" && name == "defines") {
            config.defines = parse_string_array(path, line, value);
        } else if (section == "cc" && name == "flags") {
            config.flags = parse_string_array(path, line, value);
        } else if (section == "link" && name == "flags") {
            config.link_flags = parse_string_array(path, line, value);
        } else if (section == "pkg_config" && name == "packages") {
            config.pkg_config_packages = parse_string_array(path, line, value);
        } else if (section == "pkg" && name == "libs") {
            config.pkg_config_packages = parse_string_array(path, line, value);
        } else if (section == "build" && name == "dir") {
            config.build_dir = unquote(value);
        } else if (section == "cmake" && name == "enabled") {
            config.cmake_enabled = parse_bool_value(path, line, value);
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
