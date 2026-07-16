#include "dudu/lsp/language_server_header_definition.hpp"

#include "dudu/core/text.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/native/native_build.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

bool range_contains_position(const SourceRange& range, int line, int character) {
    if (range.start.line <= 0 || range.start.column <= 0 || range.end.line <= 0 ||
        range.end.column <= 0) {
        return false;
    }
    const int start_line = range.start.line - 1;
    const int start_character = range.start.column - 1;
    const int end_line = range.end.line - 1;
    const int end_character = range.end.column - 1;
    if (line < start_line || line > end_line) {
        return false;
    }
    if (line == start_line && character < start_character) {
        return false;
    }
    return line != end_line || character <= end_character;
}

std::string unquote_header(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::vector<std::filesystem::path> pkg_config_include_dirs(const ProjectConfig& config) {
    if (config.pkg_config_packages.empty()) {
        return {};
    }
    const char* pkg_config = std::getenv("PKG_CONFIG");
    const std::string executable = pkg_config == nullptr ? "pkg-config" : std::string(pkg_config);
    std::string command = shell_quote_arg(executable) + " --cflags";
    for (const std::string& package : config.pkg_config_packages) {
        command += " " + shell_quote_arg(package);
    }
    command += " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }
    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    if (pclose(pipe) != 0) {
        return {};
    }
    std::vector<std::filesystem::path> out;
    std::istringstream flags(output);
    std::string flag;
    while (flags >> flag) {
        if (starts_with(flag, "-I") && flag.size() > 2) {
            out.emplace_back(flag.substr(2));
        }
    }
    return out;
}

std::vector<std::filesystem::path> compiler_include_dirs(const ProjectConfig& config) {
    static std::map<std::string, std::vector<std::filesystem::path>> cache;
    const std::string compiler = config.compiler.empty() ? "c++" : config.compiler;
    const std::string key = compiler + "|" + config.cpp_std;
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }
    const std::string command = shell_quote_arg(compiler) +
                                " -std=" + shell_quote_arg(config.cpp_std) +
                                " -E -x c++ /dev/null -v 2>&1 >/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }
    std::string output;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    if (pclose(pipe) != 0) {
        return {};
    }
    std::vector<std::filesystem::path> out;
    bool in_search = false;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string trimmed = trim_string(line);
        if (trimmed == "#include <...> search starts here:") {
            in_search = true;
            continue;
        }
        if (in_search && trimmed == "End of search list.") {
            break;
        }
        if (in_search && !trimmed.empty() && !starts_with(trimmed, "(")) {
            out.emplace_back(trimmed);
        }
    }
    cache[key] = out;
    return out;
}

std::optional<std::filesystem::path> resolve_header_path(const std::filesystem::path& source_dir,
                                                         const ProjectConfig& config,
                                                         const std::string& header) {
    const std::filesystem::path header_path = header;
    std::vector<std::filesystem::path> roots;
    if (header_path.is_absolute()) {
        roots.push_back({});
    } else {
        roots.push_back(source_dir);
        for (const std::string& include_dir : config.include_dirs) {
            roots.push_back(project_path(config, include_dir));
        }
        for (const std::filesystem::path& include_dir : pkg_config_include_dirs(config)) {
            roots.push_back(include_dir);
        }
        for (const std::filesystem::path& include_dir : compiler_include_dirs(config)) {
            roots.push_back(include_dir);
        }
    }
    for (const std::filesystem::path& root : roots) {
        std::filesystem::path candidate = root.empty() ? header_path : root / header_path;
        std::error_code error;
        if (std::filesystem::exists(candidate, error) && !error) {
            const std::filesystem::path resolved =
                std::filesystem::weakly_canonical(candidate, error);
            return error ? candidate.lexically_normal() : resolved;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string>
native_header_definition_json(const Document& doc, const ModuleAst& current, const Json* params) {
    const LspPosition position = lsp_position(params);
    for (const ImportDecl& import : current.imports) {
        if (import.kind != ImportKind::ForeignC && import.kind != ImportKind::ForeignCxx &&
            import.kind != ImportKind::ForeignCpp) {
            continue;
        }
        if (!range_contains_position(import.module_range, position.line, position.character) &&
            !range_contains_position(import.alias_range, position.line, position.character)) {
            continue;
        }
        const ProjectConfig config = config_for_file(doc.path);
        const std::optional<std::filesystem::path> resolved =
            resolve_header_path(doc.path.parent_path(), config, unquote_header(import.module_path));
        if (!resolved) {
            return std::nullopt;
        }
        return location_json(file_uri(*resolved), range_json(0, 0, 0));
    }
    return std::nullopt;
}

} // namespace dudu
