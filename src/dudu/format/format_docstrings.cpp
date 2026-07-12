#include "dudu/format/format_docstrings.hpp"

#include "dudu/parser/parser_doc_comments.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

struct TripleString {
    size_t begin = 0;
    size_t end = 0;
    bool docstring = false;
};

bool whitespace_only(std::string_view text) {
    return std::all_of(text.begin(), text.end(),
                       [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
}

bool standalone_string(std::string_view source, size_t begin, size_t end) {
    const size_t line_begin = source.rfind('\n', begin);
    const size_t prefix_begin = line_begin == std::string_view::npos ? 0 : line_begin + 1;
    if (!whitespace_only(source.substr(prefix_begin, begin - prefix_begin))) {
        return false;
    }

    const size_t line_end = source.find('\n', end);
    std::string_view suffix =
        source.substr(end, (line_end == std::string_view::npos ? source.size() : line_end) - end);
    const size_t comment = suffix.find('#');
    if (comment != std::string_view::npos) {
        suffix = suffix.substr(0, comment);
    }
    return whitespace_only(suffix);
}

std::vector<TripleString> find_triple_strings(std::string_view source) {
    std::vector<TripleString> strings;
    size_t cursor = 0;
    while (cursor < source.size()) {
        const char current = source[cursor];
        if (current == '#') {
            const size_t newline = source.find('\n', cursor);
            cursor = newline == std::string_view::npos ? source.size() : newline + 1;
            continue;
        }
        if (current != '\'' && current != '"') {
            ++cursor;
            continue;
        }

        const char quote = current;
        const bool triple = cursor + 2 < source.size() && source[cursor + 1] == quote &&
                            source[cursor + 2] == quote;
        if (!triple) {
            ++cursor;
            bool escaped = false;
            while (cursor < source.size()) {
                const char value = source[cursor++];
                if (escaped) {
                    escaped = false;
                } else if (value == '\\') {
                    escaped = true;
                } else if (value == quote || value == '\n' || value == '\r') {
                    break;
                }
            }
            continue;
        }

        const size_t begin = cursor;
        cursor += 3;
        while (cursor + 2 < source.size() &&
               !(source[cursor] == quote && source[cursor + 1] == quote &&
                 source[cursor + 2] == quote)) {
            ++cursor;
        }
        if (cursor + 2 >= source.size()) {
            strings.push_back({.begin = begin, .end = source.size(), .docstring = false});
            break;
        }
        cursor += 3;
        strings.push_back({.begin = begin,
                           .end = cursor,
                           .docstring = quote == '\'' && standalone_string(source, begin, cursor)});
    }
    return strings;
}

std::string normalized_indent(std::string_view indent) {
    std::string result;
    for (const char c : indent) {
        result += c == '\t' ? "    " : std::string(1, c);
    }
    return result;
}

std::string render_docstring(std::string_view source, const TripleString& string) {
    const size_t line_begin = source.rfind('\n', string.begin);
    const size_t indent_begin = line_begin == std::string_view::npos ? 0 : line_begin + 1;
    const std::string indent =
        normalized_indent(source.substr(indent_begin, string.begin - indent_begin));
    const std::string body =
        normalize_docstring_text(source.substr(string.begin + 3, string.end - string.begin - 6));
    const bool was_multiline =
        source.substr(string.begin, string.end - string.begin).find('\n') != std::string_view::npos;
    if (!was_multiline || body.empty()) {
        return "'''" + body + "'''";
    }

    std::string result = "'''";
    size_t cursor = 0;
    bool first = true;
    while (cursor <= body.size()) {
        const size_t end = body.find('\n', cursor);
        const std::string_view line = end == std::string::npos
                                          ? std::string_view(body).substr(cursor)
                                          : std::string_view(body).substr(cursor, end - cursor);
        if (first) {
            result += line;
            first = false;
        } else {
            result += '\n';
            if (!line.empty()) {
                result += indent;
                result += line;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        cursor = end + 1;
    }
    result += '\n';
    result += indent;
    result += "'''";
    return result;
}

std::pair<std::string, std::vector<TripleString>> normalize_docstrings(std::string_view source) {
    const std::vector<TripleString> original = find_triple_strings(source);
    std::string normalized;
    normalized.reserve(source.size());
    std::vector<TripleString> strings;
    size_t cursor = 0;
    for (const TripleString& string : original) {
        normalized.append(source.substr(cursor, string.begin - cursor));
        const size_t begin = normalized.size();
        if (string.docstring) {
            normalized += render_docstring(source, string);
        } else {
            normalized.append(source.substr(string.begin, string.end - string.begin));
        }
        strings.push_back(
            {.begin = begin, .end = normalized.size(), .docstring = string.docstring});
        cursor = string.end;
    }
    normalized.append(source.substr(cursor));
    return {std::move(normalized), std::move(strings)};
}

struct LineRange {
    size_t begin = 0;
    size_t end = 0;
};

std::vector<LineRange> source_line_ranges(std::string_view source) {
    std::vector<LineRange> ranges;
    size_t begin = 0;
    while (begin < source.size()) {
        const size_t newline = source.find('\n', begin);
        const size_t end = newline == std::string_view::npos ? source.size() : newline;
        ranges.push_back({.begin = begin, .end = end});
        if (newline == std::string_view::npos) {
            break;
        }
        begin = newline + 1;
    }
    return ranges;
}

} // namespace

std::vector<FormatLine> prepare_format_lines(std::string_view source) {
    auto [normalized, strings] = normalize_docstrings(source);
    const std::vector<LineRange> ranges = source_line_ranges(normalized);
    std::vector<FormatLine> lines;
    lines.reserve(ranges.size());
    for (const LineRange& range : ranges) {
        lines.push_back({.text = normalized.substr(range.begin, range.end - range.begin)});
    }

    for (const TripleString& string : strings) {
        for (size_t index = 0; index < ranges.size(); ++index) {
            const LineRange& range = ranges[index];
            if (range.end < string.begin || range.begin >= string.end) {
                continue;
            }
            lines[index].preserve_blank = true;
            if (!string.docstring && range.begin > string.begin) {
                lines[index].normalize_indent = false;
            }
            if (!string.docstring && range.end < string.end) {
                lines[index].trim_end = false;
            }
        }
    }
    return lines;
}

} // namespace dudu
