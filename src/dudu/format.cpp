#include "dudu/format.hpp"

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

bool starts_with_import(const std::string& line) {
    return line.rfind("import ", 0) == 0 || line.rfind("from ", 0) == 0;
}

void sort_leading_imports(std::vector<std::string>& lines) {
    size_t start = 0;
    while (start < lines.size() && lines[start].empty()) {
        ++start;
    }
    if (start >= lines.size() || !starts_with_import(lines[start])) {
        return;
    }
    size_t end = start;
    while (end < lines.size() && starts_with_import(lines[end])) {
        ++end;
    }
    std::sort(lines.begin() + static_cast<std::ptrdiff_t>(start),
              lines.begin() + static_cast<std::ptrdiff_t>(end));
}

} // namespace

std::string format_source(std::string_view source) {
    std::istringstream in{std::string(source)};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        rtrim(line);
        lines.push_back(std::move(line));
    }
    sort_leading_imports(lines);

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
