#include "dudu/format.hpp"

#include "dudu/format_imports.hpp"
#include "dudu/parser.hpp"

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
    std::istringstream in{std::string(source)};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        rtrim(line);
        normalize_leading_indent(line);
        lines.push_back(std::move(line));
    }
    sort_leading_imports(lines, join_lines(lines));

    std::ostringstream out;
    int blank_count = 0;
    for (const std::string& formatted_line : lines) {
        line = formatted_line;
        if (line.empty()) {
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
