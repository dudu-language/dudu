#include "dudu/lsp/language_server_diagnostics.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/lsp/language_server_ast_lints.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/native/native_build.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

std::string diagnostic_source(std::string_view code) {
    if (code == "dudu.native_header.scan_failed") {
        return "dudu/native-header";
    }
    if (code.starts_with("dudu.sema.")) {
        return "dudu/sema";
    }
    if (code.starts_with("dudu.parser.") || code.starts_with("dudu.lexer.")) {
        return "dudu/parser";
    }
    return "dudu/compiler";
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

} // namespace

std::vector<Diagnostic> diagnostics_for_document(const Document& doc) {
    try {
        ProjectConfig config;
        try {
            config = config_for_file(doc.path);
        } catch (const std::exception& error) {
            return {
                {.location = {.file = SourceFileName(doc.path.string()), .line = 1, .column = 1},
                 .message = error.what(),
                 .source = "dudu/build-config",
                 .severity = 1,
                 .code = "",
                 .data_name = "",
                 .fix_range = std::nullopt}};
        }
        if (const std::optional<std::string> missing = missing_pkg_config_package(config)) {
            return {
                {.location = {.file = SourceFileName(doc.path.string()), .line = 1, .column = 1},
                 .message = "missing pkg-config package: " + *missing,
                 .source = "dudu/build-config",
                 .severity = 1,
                 .code = "",
                 .data_name = "",
                 .fix_range = std::nullopt}};
        }
        const ProjectIndex& index = project_index_for_document(doc, true, true);
        return ast_lint_diagnostics(index.merged_module(), doc);
    } catch (const CompileError& error) {
        return {{.location = error.location(),
                 .message = error.what(),
                 .source = diagnostic_source(error.code()),
                 .severity = 1,
                 .code = error.code(),
                 .data_name = error.data_name(),
                 .fix_range = std::nullopt}};
    } catch (const std::exception& error) {
        return {{.location = {.file = SourceFileName(doc.path.string()), .line = 1, .column = 1},
                 .message = error.what(),
                 .source = "dudu/lsp",
                 .severity = 1,
                 .code = "",
                 .data_name = "",
                 .fix_range = std::nullopt}};
    }
}

std::string diagnostic_json(const Diagnostic& diagnostic) {
    const int line = std::max(0, diagnostic.location.line - 1);
    const int column = std::max(0, diagnostic.location.column - 1);
    std::ostringstream out;
    out << "{\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << column
        << "},\"end\":{\"line\":" << line << ",\"character\":" << (column + 1)
        << "}},\"severity\":" << diagnostic.severity << ",\"source\":\""
        << json_escape(diagnostic.source) << "\"";
    if (!diagnostic.code.empty()) {
        out << ",\"code\":\"" << json_escape(diagnostic.code) << "\"";
    }
    if (!diagnostic.data_name.empty() || diagnostic.fix_range.has_value()) {
        out << ",\"data\":{";
        bool first = true;
        if (!diagnostic.data_name.empty()) {
            out << "\"name\":\"" << json_escape(diagnostic.data_name) << "\"";
            first = false;
        }
        if (diagnostic.fix_range.has_value()) {
            if (!first) {
                out << ",";
            }
            out << "\"fixRange\":" << range_json(*diagnostic.fix_range);
        }
        out << "}";
    }
    out << ",\"message\":\"" << json_escape(diagnostic.message) << "\"}";
    return out.str();
}

} // namespace dudu
