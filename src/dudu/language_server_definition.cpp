#include "dudu/language_server_definition.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_local_context.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::optional<std::string> header_import_at(const std::string& line, int character) {
    static const std::regex import_regex(R"DD(^\s*import\s+(?:c|cpp)\s+"([^"]+)")DD");
    std::smatch match;
    if (!std::regex_search(line, match, import_regex)) {
        return std::nullopt;
    }
    const int start = static_cast<int>(match.position(1));
    const int end = start + static_cast<int>(match.length(1));
    return character >= start && character <= end ? std::optional<std::string>{match[1].str()}
                                                  : std::nullopt;
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
    }
    for (std::filesystem::path root : roots) {
        std::filesystem::path candidate = root.empty() ? header_path : root / header_path;
        std::error_code error;
        if (std::filesystem::exists(candidate, error) && !error) {
            const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, error);
            return error ? candidate.lexically_normal() : resolved;
        }
    }
    return std::nullopt;
}

std::optional<std::string> header_definition_json(const Document& doc, const Json* params) {
    const Json* position = params == nullptr ? nullptr : params->get("position");
    const int target_line = int_value(position == nullptr ? nullptr : position->get("line"));
    const int target_character =
        int_value(position == nullptr ? nullptr : position->get("character"));
    std::istringstream in(doc.text);
    std::string line;
    for (int row = 0; std::getline(in, line); ++row) {
        if (row != target_line) {
            continue;
        }
        const std::optional<std::string> header = header_import_at(line, target_character);
        if (!header) {
            return std::nullopt;
        }
        const ProjectConfig config = config_for_file(doc.path);
        const std::optional<std::filesystem::path> resolved =
            resolve_header_path(doc.path.parent_path(), config, *header);
        if (!resolved) {
            return std::nullopt;
        }
        return location_json(file_uri(*resolved), range_json(0, 0, 0));
    }
    return std::nullopt;
}

std::optional<std::string> member_definition_json(const Document& doc, const std::string& word) {
    const size_t dot = word.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= word.size()) {
        return std::nullopt;
    }
    const std::string receiver = word.substr(0, dot);
    const std::string member = word.substr(dot + 1);
    const std::string type = local_type_before_cursor(doc, receiver);
    if (type.empty()) {
        return std::nullopt;
    }
    try {
        ModuleAst module = parse_source(doc.text, doc.path);
        const ProjectConfig config = config_for_file(doc.path);
        merge_native_header_types(module, {.config = config, .source_dir = doc.path.parent_path()});
        const std::set<std::string> candidate_types = member_candidate_types(module, type);
        const auto find_member =
            [&](const std::vector<ClassDecl>& classes) -> std::optional<std::string> {
            for (const ClassDecl& klass : classes) {
                if (!candidate_types.contains(klass.name)) {
                    continue;
                }
                for (const FieldDecl& field : klass.fields) {
                    if (field.name == member) {
                        return location_json(uri_for_location(field.location, doc),
                                             range_json(field.location));
                    }
                }
                for (const FunctionDecl& method : klass.methods) {
                    if (method.name == member) {
                        return location_json(uri_for_location(method.location, doc),
                                             range_json(method.location));
                    }
                }
            }
            return std::nullopt;
        };
        if (const std::optional<std::string> native = find_member(module.native_classes)) {
            return native;
        }
        return find_member(module.classes);
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<std::string> import_definition_json(const Document& doc, const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const ImportDecl& import : module.imports) {
            if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
                continue;
            }
            if (import.kind == ImportKind::From && bound_import_name(import) != word) {
                continue;
            }
            std::string imported_symbol;
            if (import.kind == ImportKind::Module) {
                const std::string bound = bound_import_name(import);
                const std::vector<std::string> prefixes =
                    import.alias.empty() ? std::vector<std::string>{import.module_path, bound}
                                         : std::vector<std::string>{bound};
                bool matched = false;
                for (const std::string& prefix : prefixes) {
                    if (word == prefix) {
                        matched = true;
                        break;
                    }
                    if (word.rfind(prefix + ".", 0) == 0) {
                        imported_symbol = word.substr(prefix.size() + 1);
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    continue;
                }
            }
            const std::filesystem::path file =
                module_path_to_file(doc.path.parent_path(), import.module_path);
            std::error_code error;
            if (!std::filesystem::exists(file, error) || error) {
                continue;
            }
            if (import.kind == ImportKind::Module) {
                return location_json(file_uri(file), range_json(0, 0, 0));
            }
            std::ifstream input(file);
            if (!input) {
                continue;
            }
            const std::string text{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
            const Document imported{
                .uri = file_uri(file),
                .path = file,
                .text = text,
            };
            const std::string target =
                import.kind == ImportKind::Module ? imported_symbol : import.imported_name;
            for (const Symbol& symbol : symbols_for_document(imported, false)) {
                if (symbol.name == target) {
                    return location_json(uri_for_location(symbol.location, imported),
                                         range_json(symbol.location));
                }
            }
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

} // namespace

std::string definition_json(const Document& doc, const Json* params) {
    if (const std::optional<std::string> header = header_definition_json(doc, params)) {
        return *header;
    }
    const std::string word = symbol_at(doc, params);
    for (const Symbol& symbol : symbols_for_document(doc)) {
        if (symbol_matches(symbol.name, word)) {
            std::ostringstream out;
            out << "{\"uri\":\"" << json_escape(uri_for_location(symbol.location, doc))
                << "\",\"range\":" << range_json(symbol.location) << "}";
            return out.str();
        }
    }
    if (const std::optional<std::string> member_definition = member_definition_json(doc, word)) {
        return *member_definition;
    }
    if (const std::optional<std::string> import_definition = import_definition_json(doc, word)) {
        return *import_definition;
    }
    return "null";
}

} // namespace dudu
