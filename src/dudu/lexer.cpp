#include "dudu/lexer.hpp"

#include <cctype>
#include <string>

namespace dudu {
namespace {

class Lexer {
  public:
    Lexer(std::string_view source, std::filesystem::path file)
        : source_(source), file_(std::move(file).string()) {
    }

    std::vector<Token> run() {
        indents_.push_back(0);
        tokens_.reserve((source_.size() / 2) + 16);
        while (!done()) {
            lex_line();
        }
        close_indents();
        push(TokenKind::End, "", line_, 1);
        return std::move(tokens_);
    }

  private:
    std::string_view source_;
    SourceFileName file_;
    size_t cursor_ = 0;
    int line_ = 1;
    std::vector<int> indents_;
    std::vector<Token> tokens_;

    bool done() const {
        return cursor_ >= source_.size();
    }

    char peek(size_t offset = 0) const {
        const size_t index = cursor_ + offset;
        return index < source_.size() ? source_[index] : '\0';
    }

    char take() {
        return source_[cursor_++];
    }

    SourceLocation loc(int line, int column) const {
        return {.file = file_, .line = line, .column = column};
    }

    [[noreturn]] void error(int column, const std::string& message) const {
        throw CompileError(loc(line_, column), message, "dudu.lexer.syntax");
    }

    void push(TokenKind kind, std::string_view text, int line, int column) {
        tokens_.push_back({kind, text, loc(line, column)});
    }

    void lex_line() {
        const int start_line = line_;
        int spaces = 0;
        while (peek() == ' ' || peek() == '\t') {
            if (take() == '\t') {
                error(spaces + 1, "tabs are not valid indentation");
            }
            ++spaces;
        }

        if (peek() == '\r') {
            take();
        }
        if (peek() == '\n') {
            take();
            ++line_;
            return;
        }
        if (peek() == '#') {
            skip_to_next_line();
            return;
        }

        if (spaces % 4 != 0) {
            error(1, "indentation must be a multiple of four spaces");
        }
        update_indent(spaces / 4, start_line);

        int column = spaces + 1;
        while (!done() && peek() != '\n' && peek() != '\r') {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                take();
                ++column;
                continue;
            }
            if (c == '#') {
                skip_to_next_line();
                return;
            }
            column += lex_token(column);
        }
        if (peek() == '\r') {
            take();
        }
        if (peek() == '\n') {
            take();
        }
        push(TokenKind::Newline, "", start_line, column);
        ++line_;
    }

    void update_indent(int indent, int source_line) {
        const int current = indents_.back();
        if (indent == current) {
            return;
        }
        if (indent > current) {
            indents_.push_back(indent);
            push(TokenKind::Indent, "", source_line, 1);
            return;
        }
        while (indents_.size() > 1 && indents_.back() > indent) {
            indents_.pop_back();
            push(TokenKind::Dedent, "", source_line, 1);
        }
        if (indents_.back() != indent) {
            throw CompileError(loc(source_line, 1), "dedent does not match an active block");
        }
    }

    void close_indents() {
        while (indents_.size() > 1) {
            indents_.pop_back();
            push(TokenKind::Dedent, "", line_, 1);
        }
    }

    void skip_to_next_line() {
        while (!done() && peek() != '\n') {
            take();
        }
        if (peek() == '\n') {
            take();
            ++line_;
        }
    }

    int lex_token(int column) {
        const char c = peek();
        if (is_identifier_start(c)) {
            return lex_identifier(column);
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            return lex_number(column);
        }
        if (c == '"' || c == '\'') {
            return lex_string(column);
        }
        if (peek() == '-' && peek(1) == '>') {
            const size_t start = cursor_;
            take();
            take();
            push(TokenKind::Arrow, source_.substr(start, 2), line_, column);
            return 2;
        }
        const std::string_view three = source_.substr(cursor_, 3);
        if (three == "<<=" || three == ">>=") {
            const size_t start = cursor_;
            take();
            take();
            take();
            push(TokenKind::Operator, source_.substr(start, 3), line_, column);
            return 3;
        }
        const std::string_view two = source_.substr(cursor_, 2);
        if (two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "+=" ||
            two == "-=" || two == "*=" || two == "/=" || two == "%=" || two == "&=" ||
            two == "|=" || two == "^=" || two == "<<" || two == ">>") {
            const size_t start = cursor_;
            take();
            take();
            push(TokenKind::Operator, source_.substr(start, 2), line_, column);
            return 2;
        }
        return lex_punctuation(column);
    }

    static bool is_identifier_start(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    static bool is_identifier_char(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    int lex_identifier(int column) {
        const size_t start = cursor_;
        take();
        while (is_identifier_char(peek())) {
            take();
        }
        push(TokenKind::Identifier, source_.substr(start, cursor_ - start), line_, column);
        return static_cast<int>(cursor_ - start);
    }

    int lex_number(int column) {
        const size_t start = cursor_;
        take();
        while (std::isalnum(static_cast<unsigned char>(peek())) != 0 || peek() == '_' ||
               peek() == '.') {
            take();
        }
        push(TokenKind::Number, source_.substr(start, cursor_ - start), line_, column);
        return static_cast<int>(cursor_ - start);
    }

    int lex_string(int column) {
        const char quote = take();
        const size_t start = cursor_ - 1;
        const int start_line = line_;
        const bool triple = peek() == quote && peek(1) == quote;
        if (triple) {
            take();
            take();
            while (!done()) {
                const char c = take();
                if (c == '\n') {
                    ++line_;
                }
                if (c == quote && peek() == quote && peek(1) == quote) {
                    take();
                    take();
                    push(TokenKind::String, source_.substr(start, cursor_ - start), start_line,
                         column);
                    return static_cast<int>(cursor_ - start);
                }
            }
            throw CompileError(loc(start_line, column), "unterminated triple string literal");
        }
        bool escaped = false;
        while (!done()) {
            const char c = take();
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == quote) {
                push(TokenKind::String, source_.substr(start, cursor_ - start), line_, column);
                return static_cast<int>(cursor_ - start);
            }
            if (c == '\n' || c == '\r') {
                error(column, "unterminated string literal");
            }
        }
        error(column, "unterminated string literal");
    }

    int lex_punctuation(int column) {
        const size_t start = cursor_;
        const char c = take();
        const std::string_view text = source_.substr(start, 1);
        switch (c) {
        case '(':
            push(TokenKind::LParen, text, line_, column);
            return 1;
        case ')':
            push(TokenKind::RParen, text, line_, column);
            return 1;
        case '[':
            push(TokenKind::LBracket, text, line_, column);
            return 1;
        case ']':
            push(TokenKind::RBracket, text, line_, column);
            return 1;
        case '{':
            push(TokenKind::LBrace, text, line_, column);
            return 1;
        case '}':
            push(TokenKind::RBrace, text, line_, column);
            return 1;
        case ',':
            push(TokenKind::Comma, text, line_, column);
            return 1;
        case ':':
            push(TokenKind::Colon, text, line_, column);
            return 1;
        case '.':
            push(TokenKind::Dot, text, line_, column);
            return 1;
        case '=':
            push(TokenKind::Assign, text, line_, column);
            return 1;
        case '@':
            push(TokenKind::At, text, line_, column);
            return 1;
        default:
            push(TokenKind::Operator, text, line_, column);
            return 1;
        }
    }
};

} // namespace

std::vector<Token> lex_source(std::string_view source, const std::filesystem::path& file) {
    return Lexer(source, file).run();
}

} // namespace dudu
