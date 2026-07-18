#include "dudu/lsp/language_server_diagnostics.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/lsp/language_server_ast_lints.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_macro_diagnostics.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <tuple>

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
    if (code.starts_with("dudu.macro")) {
        return "dudu/macro";
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

Diagnostic parser_diagnostic(const ParseDiagnostic& diagnostic) {
    return {.location = diagnostic.location,
            .message = diagnostic.message,
            .source = diagnostic_source(diagnostic.code),
            .severity = 1,
            .code = diagnostic.code,
            .data_name = diagnostic.data_name,
            .fix_range = std::nullopt,
            .related_information = {}};
}

Diagnostic compile_diagnostic(const CompileError& error) {
    Diagnostic diagnostic{.location = error.location(),
                          .message = error.what(),
                          .source = diagnostic_source(error.code()),
                          .severity = 1,
                          .code = error.code(),
                          .data_name = error.data_name(),
                          .fix_range = std::nullopt,
                          .related_information = {}};
    for (const CompileNote& note : error.notes())
        diagnostic.related_information.push_back(
            {.location = note.location, .message = note.message});
    return diagnostic;
}

bool location_belongs_to_document(const SourceLocation& location, const Document& doc) {
    if (location.file.empty()) {
        return true;
    }
    std::error_code error;
    const std::filesystem::path location_path =
        std::filesystem::weakly_canonical(std::string(location.file), error);
    if (error) {
        return std::filesystem::path(std::string(location.file)).lexically_normal() ==
               doc.path.lexically_normal();
    }
    const std::filesystem::path document_path = std::filesystem::weakly_canonical(doc.path, error);
    return !error && location_path == document_path;
}

bool position_precedes(int left_line, int left_column, int right_line, int right_column) {
    return std::tie(left_line, left_column) < std::tie(right_line, right_column);
}

bool location_in_range(const SourceLocation& location, const SourceRange& range) {
    if (range.start.line <= 0 || range.end.line <= 0) {
        return false;
    }
    if (!location.file.empty() && !range.start.file.empty() && location.file != range.start.file) {
        return false;
    }
    return !position_precedes(location.line, location.column, range.start.line,
                              range.start.column) &&
           position_precedes(location.line, location.column, range.end.line, range.end.column);
}

bool range_contains_parse_error(const SourceRange& range,
                                const std::vector<ParseDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const ParseDiagnostic& item) {
        return location_in_range(item.location, range);
    });
}

void suppress_damaged_function_bodies(ModuleAst& module,
                                      const std::vector<ParseDiagnostic>& diagnostics) {
    for (FunctionDecl& function : module.functions) {
        if (range_contains_parse_error(function.range, diagnostics)) {
            function.body_syntax_damaged = true;
        }
    }
    for (ClassDecl& klass : module.classes) {
        for (FunctionDecl& method : klass.methods) {
            if (range_contains_parse_error(method.range, diagnostics)) {
                method.body_syntax_damaged = true;
            }
        }
    }
    for (ModuleAst& unit : module.module_units) {
        suppress_damaged_function_bodies(unit, diagnostics);
    }
}

std::vector<Diagnostic> diagnostics_from_index(const ProjectIndex& index, const Document& doc) {
    std::vector<Diagnostic> diagnostics;
    for (const ParseDiagnostic& diagnostic : index.parse_diagnostics()) {
        if (location_belongs_to_document(diagnostic.location, doc)) {
            diagnostics.push_back(parser_diagnostic(diagnostic));
        }
    }
    ModuleAst diagnostic_module = index.merged_module();
    suppress_damaged_function_bodies(diagnostic_module, index.parse_diagnostics());
    const std::vector<CompileError> semantic =
        analyze_module_tree_collecting(diagnostic_module, {.check_bodies = true});
    for (const CompileError& error : semantic) {
        if (location_belongs_to_document(error.location(), doc)) {
            diagnostics.push_back(compile_diagnostic(error));
        }
    }
    std::vector<Diagnostic> macro_diagnostics =
        macro_diagnostics_for_document(index.macro_report(), doc);
    diagnostics.insert(diagnostics.end(), std::make_move_iterator(macro_diagnostics.begin()),
                       std::make_move_iterator(macro_diagnostics.end()));
    std::vector<Diagnostic> lints = ast_lint_diagnostics(diagnostic_module, doc);
    diagnostics.insert(diagnostics.end(), std::make_move_iterator(lints.begin()),
                       std::make_move_iterator(lints.end()));
    return diagnostics;
}

std::vector<Diagnostic> config_diagnostics(const ProjectConfig& config, const Document& doc) {
    if (const std::optional<std::string> missing = missing_pkg_config_package(config)) {
        return {{.location = {.file = SourceFileName(doc.path.string()), .line = 1, .column = 1},
                 .message = "missing pkg-config package: " + *missing,
                 .source = "dudu/build-config",
                 .severity = 1,
                 .code = "",
                 .data_name = "",
                 .fix_range = std::nullopt,
                 .related_information = {}}};
    }
    return {};
}

std::vector<Diagnostic> exception_diagnostic(const Document& doc, const CompileError& error) {
    if (error.code().starts_with("dudu.parser.") || error.code().starts_with("dudu.lexer.")) {
        const ParseResult recovered = parse_source_recovering(doc.text, doc.path);
        if (!recovered.diagnostics.empty()) {
            std::vector<Diagnostic> diagnostics;
            diagnostics.reserve(recovered.diagnostics.size());
            for (const ParseDiagnostic& diagnostic : recovered.diagnostics) {
                diagnostics.push_back(parser_diagnostic(diagnostic));
            }
            return diagnostics;
        }
    }
    return {{.location = error.location(),
             .message = error.what(),
             .source = diagnostic_source(error.code()),
             .severity = 1,
             .code = error.code(),
             .data_name = error.data_name(),
             .fix_range = std::nullopt,
             .related_information = {}}};
}

void attach_token_ranges(const Document& doc, std::vector<Diagnostic>& diagnostics) {
    const LexResult lexed = lex_source_recovering(doc.text, doc.path);
    for (Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.range.has_value() || diagnostic.location.line <= 0 ||
            diagnostic.location.column <= 0) {
            continue;
        }
        for (const Token& token : lexed.tokens) {
            if (token.kind == TokenKind::End || token.kind == TokenKind::Indent ||
                token.kind == TokenKind::Dedent || token.kind == TokenKind::Newline ||
                token.location.line != diagnostic.location.line) {
                continue;
            }
            const SourceLocation end = token_end_location(token);
            if (diagnostic.location.column < token.location.column ||
                diagnostic.location.column >= end.column) {
                continue;
            }
            diagnostic.range = SourceRange{.start = token.location, .end = end};
            break;
        }
    }
}

} // namespace

static std::vector<Diagnostic> diagnostics_for_document_unranged(const Document& doc) {
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
                 .fix_range = std::nullopt,
                 .related_information = {}}};
        }
        if (std::vector<Diagnostic> diagnostics = config_diagnostics(config, doc);
            !diagnostics.empty()) {
            return diagnostics;
        }
        const ProjectIndexSnapshot index = project_index_for_document(doc, true, false, false);
        return diagnostics_from_index(*index, doc);
    } catch (const CompileError& error) {
        return exception_diagnostic(doc, error);
    } catch (const std::exception& error) {
        return {{.location = {.file = SourceFileName(doc.path.string()), .line = 1, .column = 1},
                 .message = error.what(),
                 .source = "dudu/lsp",
                 .severity = 1,
                 .code = "",
                 .data_name = "",
                 .fix_range = std::nullopt,
                 .related_information = {}}};
    }
}

std::vector<Diagnostic> diagnostics_for_document(const Document& doc) {
    std::vector<Diagnostic> diagnostics = diagnostics_for_document_unranged(doc);
    attach_token_ranges(doc, diagnostics);
    return diagnostics;
}

std::vector<Diagnostic> syntax_diagnostics_for_document(const Document& doc) {
    const ParseResult recovered = parse_source_recovering(doc.text, doc.path);
    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(recovered.diagnostics.size());
    for (const ParseDiagnostic& diagnostic : recovered.diagnostics) {
        diagnostics.push_back(parser_diagnostic(diagnostic));
    }
    attach_token_ranges(doc, diagnostics);
    return diagnostics;
}

static std::vector<Diagnostic> diagnostics_for_document_snapshot_unranged(
    const Document& doc, const std::map<std::filesystem::path, std::string>& source_overrides) {
    try {
        ProjectIndexOptions options;
        try {
            options = project_index_options_for_document(doc, true, false, source_overrides);
        } catch (const std::exception& error) {
            return {
                {.location = {.file = SourceFileName(doc.path.string()), .line = 1, .column = 1},
                 .message = error.what(),
                 .source = "dudu/build-config",
                 .severity = 1,
                 .code = "",
                 .data_name = "",
                 .fix_range = std::nullopt,
                 .related_information = {}}};
        }
        if (std::vector<Diagnostic> diagnostics = config_diagnostics(options.config, doc);
            !diagnostics.empty()) {
            return diagnostics;
        }
        try {
            return diagnostics_from_index(ProjectIndex::load(options), doc);
        } catch (const std::exception&) {
            const std::exception_ptr original = std::current_exception();
            try {
                options.recover_syntax = true;
                return diagnostics_from_index(ProjectIndex::load(options), doc);
            } catch (const std::exception&) {
                std::rethrow_exception(original);
            }
        }
    } catch (const CompileError& error) {
        return exception_diagnostic(doc, error);
    } catch (const std::exception& error) {
        return {{.location = {.file = SourceFileName(doc.path.string()), .line = 1, .column = 1},
                 .message = error.what(),
                 .source = "dudu/lsp",
                 .severity = 1,
                 .code = "",
                 .data_name = "",
                 .fix_range = std::nullopt,
                 .related_information = {}}};
    }
}

std::vector<Diagnostic> diagnostics_for_document_snapshot(
    const Document& doc, const std::map<std::filesystem::path, std::string>& source_overrides) {
    std::vector<Diagnostic> diagnostics =
        diagnostics_for_document_snapshot_unranged(doc, source_overrides);
    attach_token_ranges(doc, diagnostics);
    return diagnostics;
}

std::string diagnostic_json(const Diagnostic& diagnostic) {
    const int line = std::max(0, diagnostic.location.line - 1);
    const int column = std::max(0, diagnostic.location.column - 1);
    std::ostringstream out;
    const std::string diagnostic_range =
        diagnostic.range.has_value() ? range_json(*diagnostic.range)
                                     : range_json(line, column, column + 1);
    out << "{\"range\":" << diagnostic_range << ",\"severity\":" << diagnostic.severity
        << ",\"source\":\""
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
    if (!diagnostic.related_information.empty()) {
        out << ",\"relatedInformation\":[";
        for (std::size_t index = 0; index < diagnostic.related_information.size(); ++index) {
            if (index != 0)
                out << ',';
            const DiagnosticRelatedInformation& related = diagnostic.related_information[index];
            const std::string uri = related.location.file.empty()
                                        ? std::string{}
                                        : file_uri(std::filesystem::path(related.location.file));
            out << "{\"location\":" << location_json(uri, range_json(related.location))
                << ",\"message\":\"" << json_escape(related.message) << "\"}";
        }
        out << ']';
    }
    out << ",\"message\":\"" << json_escape(diagnostic.message) << "\"}";
    return out.str();
}

} // namespace dudu
