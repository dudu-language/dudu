#include "dudu/format/format_imports.hpp"

#include <algorithm>
#include <sstream>

namespace dudu {
namespace {

bool blank_line(const std::string& line) {
    return std::all_of(line.begin(), line.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\r';
    });
}

} // namespace

std::optional<OrganizedImportBlock>
organized_leading_import_block(const std::vector<std::string>& source_lines,
                               const ModuleAst& module) {
    if (module.imports.size() < 2) {
        return std::nullopt;
    }
    size_t start = 0;
    while (start < source_lines.size() && blank_line(source_lines[start])) {
        ++start;
    }
    if (start >= source_lines.size()) {
        return std::nullopt;
    }

    std::vector<ImportDecl> imports = module.imports;
    std::sort(imports.begin(), imports.end(), [](const ImportDecl& lhs, const ImportDecl& rhs) {
        return lhs.location.line < rhs.location.line;
    });
    if (imports.front().location.line != static_cast<int>(start + 1)) {
        return std::nullopt;
    }

    std::vector<std::string> import_lines;
    size_t end = start;
    for (const ImportDecl& import : imports) {
        if (import.location.line != static_cast<int>(end + 1)) {
            break;
        }
        import_lines.push_back(render_import_decl(import));
        ++end;
    }
    if (import_lines.size() < 2) {
        return std::nullopt;
    }

    std::vector<std::string> sorted = import_lines;
    std::sort(sorted.begin(), sorted.end());
    if (sorted == import_lines) {
        return std::nullopt;
    }

    std::ostringstream replacement;
    for (const std::string& line : sorted) {
        replacement << line << '\n';
    }
    return OrganizedImportBlock{
        .start_line = start,
        .end_line = end,
        .lines = std::move(sorted),
        .replacement_text = replacement.str(),
    };
}

} // namespace dudu
