#include "dudu/language_server_definition.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
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
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

bool range_contains_position(const SourceRange& range, int line, int character) {
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
    if (line == end_line && character > end_character) {
        return false;
    }
    return true;
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
            const std::filesystem::path resolved =
                std::filesystem::weakly_canonical(candidate, error);
            return error ? candidate.lexically_normal() : resolved;
        }
    }
    return std::nullopt;
}

std::optional<std::string> header_definition_json(const Document& doc, const Json* params) {
    const LspPosition position = lsp_position(params);
    ModuleAst module;
    try {
        module = parse_source(doc.text, doc.path);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    for (const ImportDecl& import : module.imports) {
        if (import.kind != ImportKind::ForeignC && import.kind != ImportKind::ForeignCpp) {
            continue;
        }
        if (!range_contains_position(import.module_range, position.line, position.character)) {
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

std::optional<std::string> member_definition_json(const Document& doc, const ExprPath& path,
                                                  const Json* params) {
    if (path.segments.size() < 2 || path.segments.front().kind != ExprPathSegmentKind::Name ||
        path.segments.back().kind != ExprPathSegmentKind::Name) {
        return std::nullopt;
    }
    const std::string& receiver = path.segments.front().text;
    const std::string& member = path.segments.back().text;
    const TypeRef type_ref = local_type_ref_before_cursor(doc, receiver, params);
    if (!has_type_ref(type_ref)) {
        return std::nullopt;
    }
    try {
        ModuleAst module = parse_source(doc.text, doc.path);
        const ProjectConfig config = config_for_file(doc.path);
        merge_native_header_types(module, {.config = config, .source_dir = doc.path.parent_path()});
        const std::set<std::string> candidate_types = member_candidate_types(module, type_ref);
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
    const std::string word = ast_symbol_path_at(doc, params).value_or("");
    if (word.empty()) {
        return "null";
    }
    for (const Symbol& symbol : symbols_for_document(doc)) {
        if (symbol_matches(symbol.name, word)) {
            std::ostringstream out;
            out << "{\"uri\":\"" << json_escape(uri_for_location(symbol.location, doc))
                << "\",\"range\":" << range_json(symbol.location) << "}";
            return out.str();
        }
    }
    const std::optional<ExprPath> path = ast_expr_path_at(doc, params);
    if (path && path->segments.size() >= 2) {
        if (const std::optional<std::string> member_definition =
                member_definition_json(doc, *path, params)) {
            return *member_definition;
        }
    }
    if (const std::optional<std::string> import_definition = import_definition_json(doc, word)) {
        return *import_definition;
    }
    if (path && path->segments.size() >= 2) {
        const std::string path_text = render_expr_path(*path);
        if (path_text != word) {
            return import_definition_json(doc, path_text).value_or("null");
        }
    }
    return "null";
}

} // namespace dudu
