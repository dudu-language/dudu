#include "dudu/format/format.hpp"

#include "dudu/format/format_docstrings.hpp"
#include "dudu/format/format_imports.hpp"
#include "dudu/parser/parser.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

void rtrim(std::string& line) {
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.pop_back();
    }
}

void normalize_leading_indent(std::string& line) {
    std::string indent;
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
        if (line[pos] == '\t') {
            indent += "    ";
        } else {
            indent += ' ';
        }
        ++pos;
    }
    if (pos > 0) {
        line = indent + line.substr(pos);
    }
}

void sort_leading_imports(std::vector<std::string>& lines, const std::string& normalized_source) {
    ModuleAst module;
    try {
        module = parse_source(normalized_source, {});
    } catch (const std::exception&) {
        return;
    }
    const std::optional<OrganizedImportBlock> organized =
        organized_leading_import_block(lines, module);
    if (organized) {
        std::copy(organized->lines.begin(), organized->lines.end(),
                  lines.begin() + static_cast<std::ptrdiff_t>(organized->start_line));
    }
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::ostringstream out;
    for (const std::string& line : lines) {
        out << line << '\n';
    }
    return out.str();
}

} // namespace

std::string format_source(std::string_view source) {
    std::vector<FormatLine> prepared_lines = prepare_format_lines(source);
    std::vector<std::string> lines;
    lines.reserve(prepared_lines.size());
    for (FormatLine& prepared : prepared_lines) {
        if (prepared.trim_end) {
            rtrim(prepared.text);
        }
        if (prepared.normalize_indent) {
            normalize_leading_indent(prepared.text);
        }
        lines.push_back(prepared.text);
    }
    sort_leading_imports(lines, join_lines(lines));

    std::ostringstream out;
    int blank_count = 0;
    for (size_t index = 0; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        if (line.empty()) {
            if (prepared_lines[index].preserve_blank) {
                for (int i = 0; i < std::min(blank_count, 2); ++i) {
                    out << '\n';
                }
                blank_count = 0;
                out << '\n';
                continue;
            }
            ++blank_count;
            continue;
        }
        const int blanks = std::min(blank_count, 2);
        for (int i = 0; i < blanks; ++i) {
            out << '\n';
        }
        blank_count = 0;
        out << line << '\n';
    }
    return out.str();
}

} // namespace dudu
