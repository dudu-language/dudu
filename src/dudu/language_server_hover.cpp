#include "dudu/language_server_hover.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_symbols.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string doc_comment_before(const Document& doc, int one_based_line) {
    std::vector<std::string> lines;
    std::istringstream in(doc.text);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    int row = one_based_line - 2;
    std::vector<std::string> comments;
    while (row >= 0 && row < static_cast<int>(lines.size())) {
        const std::string trimmed = trim_copy(lines[static_cast<size_t>(row)]);
        if (!starts_with(trimmed, "#")) {
            break;
        }
        comments.push_back(trim_copy(trimmed.substr(1)));
        --row;
    }
    std::reverse(comments.begin(), comments.end());
    std::ostringstream out;
    for (size_t i = 0; i < comments.size(); ++i) {
        if (i > 0) {
            out << "\n";
        }
        out << comments[i];
    }
    return out.str();
}

} // namespace

std::string hover_json(const Document& doc, const std::string& word,
                       const std::string& local_type) {
    for (const Symbol& symbol : symbols_for_document(doc)) {
        if (symbol_matches(symbol.name, word)) {
            std::string markdown = "`" + symbol.detail + "`";
            if (uri_for_location(symbol.location, doc) == doc.uri) {
                if (const std::string docs = doc_comment_before(doc, symbol.location.line);
                    !docs.empty()) {
                    markdown += "\n\n" + docs;
                }
            }
            return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
                   "\"},\"range\":" + range_json(symbol.location) + "}";
        }
    }
    if (!local_type.empty()) {
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" + json_escape(word) + ": " +
               json_escape(local_type) + "`\"}}";
    }
    return "null";
}

} // namespace dudu
