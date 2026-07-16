#include "dudu/core/text.hpp"
#include "dudu/project/project_config.hpp"

#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace dudu {
namespace {

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& message,
                       const std::string& line) {
    throw std::runtime_error(path.string() + ": " + message + ": " + line);
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

std::string unescape_basic_string(const std::filesystem::path& path, const std::string& line,
                                  std::string_view value) {
    std::string out;
    bool escaped = false;
    for (const char c : value) {
        if (escaped) {
            switch (c) {
            case 'b':
                out.push_back('\b');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case '"':
            case '\\':
                out.push_back(c);
                break;
            default:
                fail(path, "invalid string escape", line);
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        out.push_back(c);
    }
    if (escaped) {
        fail(path, "unterminated string escape", line);
    }
    return out;
}

std::string unquote(const std::filesystem::path& path, const std::string& line, std::string value) {
    value = trim_copy(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return unescape_basic_string(path, line,
                                     std::string_view(value).substr(1, value.size() - 2));
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
            out.push_back(unquote(path, line, item));
        }
        start = cursor + 1;
    }
    return out;
}

void require_true(const std::filesystem::path& path, const std::string& line,
                  const std::string& value) {
    if (trim_copy(value) != "true") {
        fail(path, "expected true", line);
    }
}

void parse_macro_capability(ProjectConfig& config, const std::filesystem::path& path,
                            const std::string& line, const std::string& name,
                            const std::string& value) {
    static const std::set<std::string> scoped = {"fs.read", "fs.write", "env.read", "process",
                                                 "network"};
    static const std::set<std::string> unscoped = {"clock", "random"};
    if (scoped.contains(name)) {
        std::vector<std::string> values = parse_string_array(path, line, value);
        if (values.empty()) {
            fail(path, "macro capability '" + name + "' requires at least one scope", line);
        }
        config.macro_capabilities[name] = std::move(values);
        return;
    }
    if (unscoped.contains(name)) {
        require_true(path, line, value);
        config.macro_capabilities[name] = {};
        return;
    }
    fail(path, "unknown macro capability '" + name + "'", line);
}

std::map<std::string, std::string> parse_inline_table(const std::filesystem::path& path,
                                                      const std::string& line, std::string value) {
    value = trim_copy(std::move(value));
    if (value.size() < 2 || value.front() != '{' || value.back() != '}') {
        fail(path, "invalid inline table", line);
    }
    value = value.substr(1, value.size() - 2);
    std::map<std::string, std::string> out;
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
            const size_t equal = item.find('=');
            if (equal == std::string::npos || equal == 0) {
                fail(path, "invalid inline table entry", line);
            }
            const std::string key = trim_string(std::string_view(item).substr(0, equal));
            if (key.empty() || out.contains(key)) {
                fail(path, "invalid inline table key", line);
            }
            out[key] = unquote(path, line, trim_string(std::string_view(item).substr(equal + 1)));
        }
        start = cursor + 1;
    }
    return out;
}

ProjectDependency parse_dependency(const std::filesystem::path& path, const std::string& line,
                                   const std::string& name, const std::string& value) {
    if (name.empty()) {
        fail(path, "invalid dependency name", line);
    }
    const std::map<std::string, std::string> fields = parse_inline_table(path, line, value);
    ProjectDependency dependency;
    dependency.name = name;
    for (const auto& [key, field_value] : fields) {
        if (key == "path") {
            dependency.path = field_value;
        } else if (key == "git") {
            dependency.git = field_value;
        } else if (key == "rev") {
            dependency.rev = field_value;
        } else if (key == "tag") {
            dependency.tag = field_value;
        } else if (key == "branch") {
            dependency.branch = field_value;
        } else {
            fail(path, "unknown dependency field '" + key + "'", line);
        }
    }
    const bool has_path = !dependency.path.empty();
    const bool has_git = !dependency.git.empty();
    if (has_path == has_git) {
        fail(path, "dependency must specify exactly one of path or git", line);
    }
    if (has_path &&
        (!dependency.rev.empty() || !dependency.tag.empty() || !dependency.branch.empty())) {
        fail(path, "path dependency cannot specify rev, tag, or branch", line);
    }
    const int git_ref_count = (dependency.rev.empty() ? 0 : 1) + (dependency.tag.empty() ? 0 : 1) +
                              (dependency.branch.empty() ? 0 : 1);
    if (git_ref_count > 1) {
        fail(path, "git dependency can specify only one of rev, tag, or branch", line);
    }
    dependency.kind = has_path ? "path" : "git";
    return dependency;
}

void validate_one_of(const std::filesystem::path& path, const std::string& line,
                     const std::string& field, const std::string& value,
                     const std::set<std::string>& allowed) {
    if (!allowed.contains(value)) {
        fail(path, "invalid [target] " + field, line);
    }
}

template <typename Target>
bool parse_native_build_entry(Target& target, const std::string& section,
                              const std::filesystem::path& path, const std::string& line,
                              const std::string& name, const std::string& value) {
    if ((section == "cc" && name == "include_dirs") || (section == "include" && name == "paths")) {
        target.include_dirs = parse_string_array(path, line, value);
    } else if (section == "sources" && name == "cpp") {
        target.cpp_sources = parse_string_array(path, line, value);
    } else if (section == "sources" && name == "c") {
        target.c_sources = parse_string_array(path, line, value);
    } else if ((section == "cc" && name == "lib_dirs") || (section == "link" && name == "paths")) {
        target.lib_dirs = parse_string_array(path, line, value);
    } else if ((section == "cc" && name == "libs") || (section == "link" && name == "libs")) {
        target.libs = parse_string_array(path, line, value);
    } else if (section == "cc" && name == "defines") {
        target.defines = parse_string_array(path, line, value);
    } else if (section == "cc" && name == "flags") {
        target.flags = parse_string_array(path, line, value);
    } else if (section == "link" && name == "flags") {
        target.link_flags = parse_string_array(path, line, value);
    } else if ((section == "pkg_config" && name == "packages") ||
               (section == "pkg" && name == "libs")) {
        target.pkg_config_packages = parse_string_array(path, line, value);
    } else if ((section == "pkg_config" || section == "pkg") && name == "paths") {
        target.pkg_config_paths = parse_string_array(path, line, value);
    } else {
        return false;
    }
    return true;
}

bool array_is_closed(const std::string& value) {
    char quote = '\0';
    bool escaped = false;
    for (const char c : value) {
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
        } else if (c == ']') {
            return true;
        }
    }
    return false;
}

} // namespace

ProjectConfig parse_project_config(const std::filesystem::path& path) {
    ProjectConfig config;
    const std::filesystem::path config_path =
        path.empty() ? std::filesystem::path("dudu.toml") : path;
    const std::filesystem::path parent =
        config_path.has_parent_path() ? config_path.parent_path() : std::filesystem::path(".");
    config.project_dir = std::filesystem::absolute(parent).lexically_normal();
    config.manifest_path = std::filesystem::absolute(config_path).lexically_normal();
    std::ifstream file(config_path);
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
            section = trim_string(std::string_view(line).substr(1, line.size() - 2));
            continue;
        }
        const size_t equal = line.find('=');
        if (equal == std::string::npos) {
            fail(path, section == "build" ? "invalid [build] entry" : "invalid entry", line);
        }
        const std::string name = trim_string(std::string_view(line).substr(0, equal));
        std::string value = trim_string(std::string_view(line).substr(equal + 1));
        if (value.starts_with("[") && !array_is_closed(value)) {
            std::string continuation;
            while (std::getline(file, continuation)) {
                continuation = trim_copy(strip_comment(std::move(continuation)));
                value += continuation;
                if (array_is_closed(value)) {
                    break;
                }
            }
        }
        if (name.empty() || value.empty()) {
            fail(path, section == "build" ? "invalid [build] entry" : "invalid entry", line);
        }
        if (section.starts_with("targets.")) {
            std::string target_name = section.substr(8);
            std::string target_section;
            if (const size_t dot = target_name.find('.'); dot != std::string::npos) {
                target_section = target_name.substr(dot + 1);
                target_name = target_name.substr(0, dot);
            }
            if (target_name.empty()) {
                fail(path, "invalid target section", line);
            }
            ProjectTarget& target = config.targets[target_name];
            if (!target_section.empty()) {
                if (!parse_native_build_entry(target, target_section, path, line, name, value)) {
                    fail(path, "unknown [" + section + "] entry", line);
                }
            } else if (name == "entry" || name == "main") {
                target.main = unquote(path, line, value);
            } else if (name == "kind") {
                target.target_kind = unquote(path, line, value);
                validate_one_of(path, line, "kind", target.target_kind,
                                {"executable", "library", "shared_library"});
            } else if (name == "mode") {
                target.target_mode = unquote(path, line, value);
                target.target_mode_explicit = true;
                validate_one_of(path, line, "mode", target.target_mode,
                                {"hosted", "freestanding", "embedded", "cuda", "shader"});
            } else {
                fail(path, "unknown [" + section + "] entry", line);
            }
        } else if (section.empty() && name == "name") {
            config.name = unquote(path, line, value);
        } else if (section.empty() && (name == "main" || name == "entry")) {
            config.main = unquote(path, line, value);
        } else if (section.empty() && name == "build_dir") {
            config.build_dir = unquote(path, line, value);
        } else if (section.empty() && name == "cpp_std") {
            config.cpp_std = unquote(path, line, value);
        } else if (section == "cxx" && name == "standard") {
            config.cpp_std = unquote(path, line, value);
        } else if (section == "cxx" && name == "compiler") {
            config.compiler = unquote(path, line, value);
        } else if (section == "target" && name == "kind") {
            config.target_kind = unquote(path, line, value);
            validate_one_of(path, line, "kind", config.target_kind,
                            {"executable", "library", "shared_library"});
        } else if (section == "target" && name == "mode") {
            config.target_mode = unquote(path, line, value);
            config.target_mode_explicit = true;
            validate_one_of(path, line, "mode", config.target_mode,
                            {"hosted", "freestanding", "embedded", "cuda", "shader"});
        } else if (section == "bench" && name == "command") {
            config.bench_command = unquote(path, line, value);
        } else if (section == "test" && name == "command") {
            config.test_command = unquote(path, line, value);
        } else if (section == "cc" && name == "compiler") {
            config.compiler = unquote(path, line, value);
        } else if (section == "deps") {
            ProjectDependency dependency = parse_dependency(path, line, name, value);
            config.dependencies[dependency.name] = std::move(dependency);
        } else if (section == "macro.capabilities") {
            parse_macro_capability(config, path, line, name, value);
        } else if (section == "macro" && name == "allow_non_cacheable") {
            const std::vector<std::string> names = parse_string_array(path, line, value);
            config.non_cacheable_macros.insert(names.begin(), names.end());
        } else if (section == "build" && name == "dir") {
            config.build_dir = unquote(path, line, value);
        } else if (section == "build" && name == "backend") {
            fail(path, "[build] backend was removed; dudu build and dudu run always use CMake",
                 line);
        } else if (section == "cmake" && name == "source") {
            config.cmake_source = unquote(path, line, value);
        } else if (section == "cmake" && name == "target") {
            config.cmake_target = unquote(path, line, value);
        } else if (section == "cmake" && name == "config") {
            config.cmake_config = unquote(path, line, value);
        } else if (section == "cmake" && name == "generator") {
            config.cmake_generator = unquote(path, line, value);
        } else if (section == "build") {
            config.build_values[name] = value;
        } else if (!parse_native_build_entry(config, section, path, line, name, value)) {
            fail(path,
                 section.empty() ? "unknown top-level entry" : "unknown [" + section + "] entry",
                 line);
        }
    }
    return config;
}

} // namespace dudu
