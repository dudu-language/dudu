#include "dudu/macro/macro_diagnostic_bridge.hpp"

namespace dudu::macro {
namespace {

void append_note(std::vector<CompileNote>& out, const protocol::Diagnostic& diagnostic,
                 const protocol::SourceRange& fallback) {
    out.push_back({.location = source_location_from_macro_range(diagnostic.range, fallback),
                   .message = diagnostic.message});
    for (const protocol::Diagnostic& note : diagnostic.notes)
        append_note(out, note, fallback);
}

std::vector<CompileNote> notes(const std::vector<protocol::Diagnostic>& diagnostics,
                               const protocol::SourceRange& fallback) {
    std::vector<CompileNote> out;
    for (const protocol::Diagnostic& diagnostic : diagnostics)
        append_note(out, diagnostic, fallback);
    return out;
}

} // namespace

SourceLocation source_location_from_macro_range(const protocol::SourceRange& range,
                                                const protocol::SourceRange& fallback) {
    const protocol::SourceRange& source = range.file.empty() ? fallback : range;
    return {.file = SourceFileName(source.file),
            .line = static_cast<int>(source.start.line),
            .column = static_cast<int>(source.start.column)};
}

CompileError compile_error_from_macro_diagnostic(const protocol::Diagnostic& diagnostic,
                                                 const protocol::SourceRange& fallback) {
    return CompileError(source_location_from_macro_range(diagnostic.range, fallback),
                        diagnostic.message,
                        diagnostic.code.empty() ? "dudu.macro" : diagnostic.code, {},
                        notes(diagnostic.notes, fallback));
}

CompileError compile_error_from_worker(const protocol::WorkerError& error,
                                       const protocol::SourceRange& invocation,
                                       const std::string& macro_name) {
    const std::string message = "macro @" + macro_name + " failed: " + error.message;
    return CompileError(source_location_from_macro_range({}, invocation), message,
                        error.code.empty() ? "dudu.macro.worker" : error.code, {},
                        notes(error.diagnostics, invocation));
}

} // namespace dudu::macro
