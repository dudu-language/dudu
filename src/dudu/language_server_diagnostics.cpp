#include "dudu/language_server_diagnostics.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

struct LintLocal {
    std::string name;
    std::string type;
    int line = 0;
    int column = 0;
    int indent = 0;
};

bool identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string diagnostic_source(std::string_view message) {
    if (message.find("could not scan native header") != std::string_view::npos) {
        return "dudu/native-header";
    }
    if (message.find("return type mismatch") != std::string_view::npos ||
        message.find("cannot assign") != std::string_view::npos ||
        message.find("unknown identifier") != std::string_view::npos ||
        message.find("unknown type") != std::string_view::npos ||
        message.find("argument ") != std::string_view::npos) {
        return "dudu/sema";
    }
    if (message.find("unexpected") != std::string_view::npos ||
        message.find("expected newline") != std::string_view::npos ||
        message.find("expected indent") != std::string_view::npos ||
        message.find("expected identifier") != std::string_view::npos) {
        return "dudu/parser";
    }
    return "dudu/sema";
}

std::optional<std::string> missing_pkg_config_package(const ProjectConfig& config) {
    const char* pkg_config = std::getenv("PKG_CONFIG");
    const std::string executable = pkg_config == nullptr ? "pkg-config" : std::string(pkg_config);
    for (const std::string& package : config.pkg_config_packages) {
        const std::string command =
            shell_quote_arg(executable) + " --exists " + shell_quote_arg(package) + " 2>/dev/null";
        if (std::system(command.c_str()) != 0) {
            return package;
        }
    }
    return std::nullopt;
}

std::string visible_local_type(const std::vector<LintLocal>& active_decls,
                               const std::string& name) {
    for (auto it = active_decls.rbegin(); it != active_decls.rend(); ++it) {
        if (it->name == name) {
            return it->type;
        }
    }
    return {};
}

bool is_suspicious_numeric_cast(const std::string& target, std::string source) {
    source = trim_copy(std::move(source));
    if (target == source) {
        return false;
    }
    static const std::map<std::string, int> integer_bits = {
        {"i8", 8},   {"u8", 8},   {"i16", 16}, {"u16", 16},   {"i32", 32},
        {"u32", 32}, {"i64", 64}, {"u64", 64}, {"isize", 64}, {"usize", 64},
    };
    const bool source_float = source == "f32" || source == "f64";
    const auto source_integer = integer_bits.find(source);
    const auto target_integer = integer_bits.find(target);
    if (source_float && target_integer != integer_bits.end()) {
        return true;
    }
    if (source == "f64" && target == "f32") {
        return true;
    }
    if (source_integer != integer_bits.end() && target_integer != integer_bits.end() &&
        target_integer->second < source_integer->second) {
        return true;
    }
    if (source_integer != integer_bits.end() && target == "f32" && source_integer->second > 24) {
        return true;
    }
    return false;
}

void lint_suspicious_casts(const std::string& line, const std::vector<LintLocal>& active_decls,
                           const Document& doc, int row, std::vector<Diagnostic>& out) {
    static const std::regex cast_call(
        R"(\b(i8|i16|i32|i64|u8|u16|u32|u64|isize|usize|f32|f64)\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))");
    for (std::sregex_iterator it(line.begin(), line.end(), cast_call), end; it != end; ++it) {
        const std::string target = (*it)[1].str();
        const std::string source_name = (*it)[2].str();
        const std::string source_type = visible_local_type(active_decls, source_name);
        if (source_type.empty() || !is_suspicious_numeric_cast(target, source_type)) {
            continue;
        }
        out.push_back({.location = {.file = doc.path,
                                    .line = row,
                                    .column = static_cast<int>(it->position(0)) + 1},
                       .message = "suspicious narrowing cast: " + target + "(" + source_name +
                                  ") from " + source_type,
                       .source = "dudu/lint",
                       .severity = 2});
    }
}

bool contains_identifier(const std::string& line, const std::string& name) {
    for (size_t start = 0; start < line.size();) {
        if (!identifier_char(line[start])) {
            ++start;
            continue;
        }
        size_t end = start + 1;
        while (end < line.size() && identifier_char(line[end])) {
            ++end;
        }
        if (line.substr(start, end - start) == name) {
            return true;
        }
        start = end;
    }
    return false;
}

std::vector<Diagnostic> lint_diagnostics(const Document& doc) {
    std::vector<Diagnostic> out;
    std::vector<std::string> lines;
    std::vector<LintLocal> locals;
    std::vector<LintLocal> active_decls;
    static const std::regex local_decl(
        R"(^(\s*)([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_\.]*)\b)");
    static const std::regex def_decl(R"(^(\s*)def\s+[A-Za-z_][A-Za-z0-9_]*\(([^)]*)\))");
    std::istringstream in(doc.text);
    std::string line;
    int row = 0;
    std::optional<int> returned_indent;
    while (std::getline(in, line)) {
        lines.push_back(line);
        ++row;
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty() || starts_with(trimmed, "#")) {
            continue;
        }
        const int indent = leading_spaces(line);
        while (!active_decls.empty() && active_decls.back().indent > indent) {
            active_decls.pop_back();
        }
        if (returned_indent && indent >= *returned_indent) {
            out.push_back({.location = {.file = doc.path, .line = row, .column = indent + 1},
                           .message = "unreachable statement after return",
                           .source = "dudu/lint",
                           .severity = 2});
            continue;
        }
        if (returned_indent && indent < *returned_indent) {
            returned_indent = std::nullopt;
        }
        if (starts_with(trimmed, "return") &&
            (trimmed.size() == 6 || std::isspace(static_cast<unsigned char>(trimmed[6])) != 0)) {
            returned_indent = indent;
        }
        std::smatch match;
        if (std::regex_search(line, match, def_decl)) {
            const std::vector<std::string> params = split_top_level_args(match[2].str());
            for (const std::string& param : params) {
                const size_t colon = param.find(':');
                if (colon == std::string::npos) {
                    continue;
                }
                const std::string name = trim_copy(param.substr(0, colon));
                const std::string type = trim_copy(param.substr(colon + 1));
                if (!name.empty() && name != "self") {
                    active_decls.push_back({.name = name,
                                            .type = type,
                                            .line = row,
                                            .column = indent + 1,
                                            .indent = indent + 4});
                }
            }
        }
        lint_suspicious_casts(line, active_decls, doc, row, out);
        if (starts_with(trimmed, "cpp(")) {
            out.push_back({.location = {.file = doc.path, .line = row, .column = indent + 1},
                           .message = "native interop hazard: raw cpp escape hatch",
                           .source = "dudu/lint",
                           .severity = 2});
        }
        if (std::regex_search(line, match, local_decl)) {
            const std::string name = match[2].str();
            const std::string type = trim_copy(match[3].str());
            for (const LintLocal& outer : active_decls) {
                if (outer.name == name && outer.line != row && outer.indent <= indent) {
                    out.push_back(
                        {.location = {.file = doc.path, .line = row, .column = indent + 1},
                         .message = "local shadows outer binding: " + name,
                         .source = "dudu/lint",
                         .severity = 2});
                    break;
                }
            }
            LintLocal local{
                .name = name, .type = type, .line = row, .column = indent + 1, .indent = indent};
            locals.push_back(local);
            active_decls.push_back(std::move(local));
        }
    }
    for (const LintLocal& local : locals) {
        bool used = false;
        for (size_t i = static_cast<size_t>(local.line); i < lines.size(); ++i) {
            if (contains_identifier(lines[i], local.name)) {
                used = true;
                break;
            }
        }
        if (!used) {
            out.push_back(
                {.location = {.file = doc.path, .line = local.line, .column = local.column},
                 .message = "unused local: " + local.name,
                 .source = "dudu/lint",
                 .severity = 2});
        }
    }
    return out;
}

} // namespace

std::vector<Diagnostic> diagnostics_for_document(const Document& doc) {
    try {
        ProjectConfig config;
        try {
            config = config_for_file(doc.path);
        } catch (const std::exception& error) {
            return {
                {{.file = doc.path, .line = 1, .column = 1}, error.what(), "dudu/build-config", 1}};
        }
        if (const std::optional<std::string> missing = missing_pkg_config_package(config)) {
            return {{{.file = doc.path, .line = 1, .column = 1},
                     "missing pkg-config package: " + *missing,
                     "dudu/build-config",
                     1}};
        }
        const bool project_tree =
            std::filesystem::exists(doc.path) && source_tree_files(doc.path).size() > 1;
        ModuleAst module =
            project_tree ? load_source_tree(doc.path) : parse_source(doc.text, doc.path);
        module.build_values = config.build_values;
        module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
        module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
        module.target_mode_explicit = config.target_mode_explicit;
        const NativeHeaderOptions native_options{.config = config,
                                                 .source_dir = doc.path.parent_path()};
        merge_native_header_types(module, native_options);
        for (ModuleAst& unit : module.module_units) {
            unit.build_values = module.build_values;
            unit.target_mode_explicit = module.target_mode_explicit;
            merge_native_header_types(unit, native_options);
        }
        if (project_tree || config.build_backend == "cmake") {
            analyze_module_tree(module, {.check_bodies = true});
        } else {
            analyze_module(module, {.check_bodies = true});
        }
        return lint_diagnostics(doc);
    } catch (const CompileError& error) {
        return {{.location = error.location(),
                 .message = error.what(),
                 .source = diagnostic_source(error.what()),
                 .severity = 1}};
    } catch (const std::exception& error) {
        return {{{.file = doc.path, .line = 1, .column = 1}, error.what(), "dudu/lsp", 1}};
    }
}

std::string diagnostic_json(const Diagnostic& diagnostic) {
    const int line = std::max(0, diagnostic.location.line - 1);
    const int column = std::max(0, diagnostic.location.column - 1);
    std::ostringstream out;
    out << "{\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << column
        << "},\"end\":{\"line\":" << line << ",\"character\":" << (column + 1)
        << "}},\"severity\":" << diagnostic.severity << ",\"source\":\""
        << json_escape(diagnostic.source) << "\",\"message\":\"" << json_escape(diagnostic.message)
        << "\"}";
    return out.str();
}

} // namespace dudu
