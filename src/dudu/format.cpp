#include "dudu/format.hpp"

#include "dudu/ast.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
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

std::string render_import_decl(const ImportDecl& import) {
    std::ostringstream out;
    if (import.kind == ImportKind::From) {
        out << "from " << import.module_path << " import " << import.imported_name;
    } else {
        out << "import ";
        if (import.kind == ImportKind::ForeignC) {
            out << "c ";
        } else if (import.kind == ImportKind::ForeignCpp) {
            out << "cpp ";
        }
        out << import.module_path;
    }
    if (!import.alias.empty()) {
        out << " as " << import.alias;
    }
    return out.str();
}

void sort_leading_imports(std::vector<std::string>& lines, const std::string& normalized_source) {
    ModuleAst module;
    try {
        module = parse_source(normalized_source, {});
    } catch (const std::exception&) {
        return;
    }
    if (module.imports.size() < 2) {
        return;
    }
    size_t start = 0;
    while (start < lines.size() && lines[start].empty()) {
        ++start;
    }
    if (start >= lines.size()) {
        return;
    }
    std::vector<ImportDecl> imports = module.imports;
    std::sort(imports.begin(), imports.end(), [](const ImportDecl& lhs, const ImportDecl& rhs) {
        return lhs.location.line < rhs.location.line;
    });
    if (imports.front().location.line != static_cast<int>(start + 1)) {
        return;
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
        return;
    }
    std::vector<std::string> sorted = import_lines;
    std::sort(sorted.begin(), sorted.end());
    if (sorted == import_lines) {
        return;
    }
    std::copy(sorted.begin(), sorted.end(), lines.begin() + static_cast<std::ptrdiff_t>(start));
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
