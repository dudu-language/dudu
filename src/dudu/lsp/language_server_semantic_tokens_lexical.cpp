#include "dudu/lsp/language_server_semantic_tokens.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

struct SemanticToken {
    int line = 0;
    int column = 0;
    int length = 0;
    int type = 0;
    int modifiers = 0;
};

constexpr int token_type = 1;
constexpr int token_class = 2;
constexpr int token_enum = 3;
constexpr int token_function = 4;
constexpr int token_variable = 6;
constexpr int token_macro = 10;
constexpr int token_keyword = 11;
constexpr int token_number = 12;
constexpr int token_string = 13;
constexpr int token_operator = 14;

constexpr int mod_readonly = 4;

bool word_is(std::string_view word, std::initializer_list<std::string_view> words) {
    for (const std::string_view candidate : words) {
        if (word == candidate) {
            return true;
        }
    }
    return false;
}

bool identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

int token_type_for_word(std::string_view word, std::optional<std::string_view> previous_keyword,
                        bool decorator) {
    if (decorator) {
        return token_macro;
    }
    if (previous_keyword == "def") {
        return token_function;
    }
    if (previous_keyword == "class") {
        return token_class;
    }
    if (previous_keyword == "enum") {
        return token_enum;
    }
    if (previous_keyword == "type") {
        return token_type;
    }
    if (word_is(word,
                {"if",     "elif",    "else",         "while",        "for",    "in",       "match",
                 "case",   "try",     "except",       "return",       "break",  "continue", "pass",
                 "raise",  "delete",  "assert",       "debug_assert", "and",    "or",       "not",
                 "def",    "class",   "enum",         "type",         "import", "from",     "as",
                 "public", "private", "static_assert"})) {
        return token_keyword;
    }
    if (word_is(word, {"bool",   "i8",       "i16",     "i32",    "i64",        "u8",    "u16",
                       "u32",    "u64",      "isize",   "usize",  "f32",        "f64",   "void",
                       "str",    "cstr",     "list",    "dict",   "set",        "tuple", "Result",
                       "Option", "fn",       "const",   "array",  "array_view", "slice", "dyn",
                       "atomic", "volatile", "storage", "shared", "device"})) {
        return token_type;
    }
    if (word_is(word, {"True", "False", "None"})) {
        return token_variable;
    }
    if (!word.empty() && std::isupper(static_cast<unsigned char>(word.front())) != 0) {
        const bool all_caps = std::all_of(word.begin(), word.end(), [](char c) {
            return std::isupper(static_cast<unsigned char>(c)) != 0 ||
                   std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '_';
        });
        return all_caps ? token_variable : token_class;
    }
    return token_variable;
}

std::string encode_tokens(std::vector<SemanticToken> tokens) {
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

void collect_string(std::string_view source, size_t& i, int& column,
                    std::vector<SemanticToken>& tokens, int line) {
    const char quote = source[i];
    const int start_column = column;
    const size_t start = i;
    const bool triple = i + 2 < source.size() && source[i + 1] == quote && source[i + 2] == quote;
    i += triple ? 3 : 1;
    column += triple ? 3 : 1;
    bool escaped = false;
    while (i < source.size()) {
        const char c = source[i];
        if (c == '\n' || c == '\r') {
            break;
        }
        ++i;
        ++column;
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (triple && c == quote && i + 1 < source.size() && source[i] == quote &&
            source[i + 1] == quote) {
            i += 2;
            column += 2;
            break;
        }
        if (!triple && c == quote) {
            break;
        }
    }
    tokens.push_back({.line = line,
                      .column = start_column,
                      .length = std::max(1, static_cast<int>(i - start)),
                      .type = token_string,
                      .modifiers = 0});
}

void collect_number(std::string_view source, size_t& i, int& column,
                    std::vector<SemanticToken>& tokens, int line) {
    const int start_column = column;
    const size_t start = i;
    while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) != 0 ||
                                 source[i] == '_' || source[i] == '.')) {
        ++i;
        ++column;
    }
    tokens.push_back({.line = line,
                      .column = start_column,
                      .length = static_cast<int>(i - start),
                      .type = token_number,
                      .modifiers = 0});
}

void collect_identifier_or_decorator(std::string_view source, size_t& i, int& column,
                                     std::vector<SemanticToken>& tokens, int line,
                                     std::optional<std::string_view>& previous_keyword) {
    const bool decorator = source[i] == '@';
    const int start_column = column;
    if (decorator) {
        ++i;
        ++column;
    }
    const size_t start = i;
    if (i < source.size() && identifier_start(source[i])) {
        ++i;
        ++column;
        while (i < source.size() && (identifier_char(source[i]) || source[i] == '.')) {
            ++i;
            ++column;
        }
    }
    const std::string_view word = source.substr(start, i - start);
    if (word.empty()) {
        return;
    }
    const int type = token_type_for_word(word, previous_keyword, decorator);
    const int modifiers = word == "True" || word == "False" || word == "None" ? mod_readonly : 0;
    tokens.push_back({.line = line,
                      .column = start_column,
                      .length = static_cast<int>(i - start + (decorator ? 1 : 0)),
                      .type = type,
                      .modifiers = modifiers});
    previous_keyword = type == token_keyword ? std::optional<std::string_view>{word} : std::nullopt;
}

} // namespace

std::string lexical_semantic_tokens_json(std::string_view source) {
    std::vector<SemanticToken> tokens;
    int line = 0;
    int column = 0;
    size_t i = 0;
    std::optional<std::string_view> previous_keyword;
    while (i < source.size()) {
        const char c = source[i];
        if (c == '\r') {
            ++i;
        } else if (c == '\n') {
            ++line;
            column = 0;
            ++i;
            previous_keyword.reset();
        } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++i;
            ++column;
        } else if (c == '#') {
            while (i < source.size() && source[i] != '\n') {
                ++i;
                ++column;
            }
        } else if (c == '"' || c == '\'') {
            collect_string(source, i, column, tokens, line);
            previous_keyword.reset();
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            collect_number(source, i, column, tokens, line);
            previous_keyword.reset();
        } else if (c == '@' || identifier_start(c)) {
            collect_identifier_or_decorator(source, i, column, tokens, line, previous_keyword);
        } else {
            if (std::ispunct(static_cast<unsigned char>(c)) != 0) {
                tokens.push_back({.line = line,
                                  .column = column,
                                  .length = 1,
                                  .type = token_operator,
                                  .modifiers = 0});
            }
            ++i;
            ++column;
            previous_keyword.reset();
        }
    }
    return encode_tokens(std::move(tokens));
}

} // namespace dudu
