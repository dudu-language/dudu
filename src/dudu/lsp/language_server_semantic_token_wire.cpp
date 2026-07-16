#include "dudu/lsp/language_server_semantic_token_wire.hpp"

#include <algorithm>
#include <sstream>

namespace dudu {

void add_semantic_token(std::vector<SemanticToken>& tokens, const SourceLocation& location,
                        std::string_view text, int type, int modifiers) {
    if (text.empty() || location.line <= 0 || location.column <= 0) {
        return;
    }
    tokens.push_back({.line = location.line - 1,
                      .column = location.column - 1,
                      .length = static_cast<int>(text.size()),
                      .type = type,
                      .modifiers = modifiers});
}

void add_native_semantic_token(std::vector<SemanticToken>& tokens,
                               const SourceLocation& location, std::string_view text, int type,
                               int modifiers) {
    add_semantic_token(tokens, location, text, type,
                       modifiers | semantic_token_modifier::native);
}

void add_semantic_token_range(std::vector<SemanticToken>& tokens, const SourceRange& range,
                              int type, int modifiers) {
    if (range.start.line <= 0 || range.start.column <= 0 || range.end.line != range.start.line ||
        range.end.column <= range.start.column) {
        return;
    }
    tokens.push_back({.line = range.start.line - 1,
                      .column = range.start.column - 1,
                      .length = range.end.column - range.start.column,
                      .type = type,
                      .modifiers = modifiers});
}

std::string_view semantic_token_legend_json() {
    return "{\"tokenTypes\":[\"namespace\",\"type\",\"class\",\"enum\",\"function\",\"method\","
           "\"variable\",\"parameter\",\"property\",\"enumMember\",\"macro\",\"keyword\","
           "\"number\",\"string\",\"operator\"],"
           "\"tokenModifiers\":[\"declaration\",\"definition\",\"readonly\",\"static\","
           "\"native\",\"unresolved\"]}";
}

std::string encode_semantic_tokens(std::vector<SemanticToken> tokens) {
    std::sort(tokens.begin(), tokens.end(),
              [](const SemanticToken& left, const SemanticToken& right) {
                  if (left.line != right.line) {
                      return left.line < right.line;
                  }
                  if (left.column != right.column) {
                      return left.column < right.column;
                  }
                  if (left.length != right.length) {
                      return left.length < right.length;
                  }
                  return left.type < right.type;
              });

    std::ostringstream out;
    out << "{\"data\":[";
    int previous_line = 0;
    int previous_column = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const SemanticToken& token = tokens[i];
        if (i > 0) {
            out << ',';
        }
        const int delta_line = i == 0 ? token.line : token.line - previous_line;
        const int delta_column = delta_line == 0 ? token.column - previous_column : token.column;
        out << delta_line << ',' << delta_column << ',' << token.length << ',' << token.type << ','
            << token.modifiers;
        previous_line = token.line;
        previous_column = token.column;
    }
    out << "]}";
    return out.str();
}

} // namespace dudu
