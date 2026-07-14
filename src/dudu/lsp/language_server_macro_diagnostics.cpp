#include "dudu/lsp/language_server_macro_diagnostics.hpp"

#include <filesystem>

namespace dudu {
namespace {

namespace p = macro::protocol;

SourceLocation location(const p::SourceRange& range, const p::SourceRange& fallback) {
    const p::SourceRange& source = range.file.empty() ? fallback : range;
    return {.file = SourceFileName(source.file),
            .line = static_cast<int>(source.start.line),
            .column = static_cast<int>(source.start.column)};
}

bool same_file(const SourceLocation& location, const std::filesystem::path& path) {
    if (location.file.empty())
        return true;
    std::error_code error;
    const std::filesystem::path left =
        std::filesystem::weakly_canonical(std::string(location.file), error);
    if (error)
        return std::filesystem::path(std::string(location.file)).lexically_normal() ==
               path.lexically_normal();
    const std::filesystem::path right = std::filesystem::weakly_canonical(path, error);
    return !error && left == right;
}

int severity(p::DiagnosticSeverity value) {
    if (value == p::DiagnosticSeverity::Error)
        return 1;
    if (value == p::DiagnosticSeverity::Warning)
        return 2;
    return 3;
}

void append_notes(std::vector<DiagnosticRelatedInformation>& out, const p::Diagnostic& diagnostic,
                  const p::SourceRange& fallback) {
    for (const p::Diagnostic& note : diagnostic.notes) {
        out.push_back({.location = location(note.range, fallback), .message = note.message});
        append_notes(out, note, fallback);
    }
}

} // namespace

std::vector<Diagnostic> macro_diagnostics_for_document(const macro::ExpansionReport& report,
                                                       const Document& document) {
    std::vector<Diagnostic> out;
    for (const macro::ExpansionReport::Record& record : report.expansions) {
        for (const p::Diagnostic& source : record.expansion.diagnostics) {
            const SourceLocation primary = location(source.range, record.invocation);
            if (!same_file(primary, document.path))
                continue;
            Diagnostic diagnostic{.location = primary,
                                  .message = source.message,
                                  .source = "dudu/macro",
                                  .severity = severity(source.severity),
                                  .code = source.code,
                                  .data_name = {},
                                  .fix_range = std::nullopt,
                                  .related_information = {}};
            append_notes(diagnostic.related_information, source, record.invocation);
            out.push_back(std::move(diagnostic));
        }
    }
    return out;
}

} // namespace dudu
