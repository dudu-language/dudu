#include "dudu/ast_expr.hpp"
#include "dudu/ast_expr_token_parser.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type_token_parser.hpp"

#include <algorithm>
#include <utility>

namespace dudu {

namespace {

std::string_view canonical_operator_text(std::string_view text) {
    if (text_is(text, "and")) {
        return "and";
    }
    if (text_is(text, "or")) {
        return "or";
    }
    if (text_is(text, "not")) {
        return "not";
    }
    if (text.size() == 1) {
        switch (text[0]) {
        case '*':
            return "*";
        case '-':
            return "-";
        case '&':
            return "&";
        case '~':
            return "~";
        case '+':
            return "+";
        case '/':
            return "/";
        case '%':
            return "%";
        case '<':
            return "<";
        case '>':
            return ">";
        case '|':
            return "|";
        case '^':
            return "^";
        default:
            return text;
        }
    }
    if (text_is(text, "==")) {
        return "==";
    }
    if (text_is(text, "!=")) {
        return "!=";
    }
    if (text_is(text, "<=")) {
        return "<=";
    }
    if (text_is(text, ">=")) {
        return ">=";
    }
    if (text_is(text, "<<")) {
        return "<<";
    }
    if (text_is(text, ">>")) {
        return ">>";
    }
    return text;
}

} // namespace

ExprTokenParser::ExprTokenParser(std::span<const Token> tokens) : tokens_(tokens) {
}

Expr ExprTokenParser::parse() {
    if (at_end()) {
        return make_expr(ExprKind::Missing, "", {});
    }
    const size_t begin = cursor_;
    Expr expr = parse_comma_expr({});
    if (!at_end()) {
        return make_node(ExprKind::Unknown, begin, tokens_.size() - 1);
    }
    expr.range = range_between(begin, cursor_);
    return expr;
}

bool ExprTokenParser::at_end() const {
    return cursor_ >= tokens_.size() || !expression_token(tokens_[cursor_]);
}

const Token& ExprTokenParser::current() const {
    return tokens_[std::min(cursor_, tokens_.size() - 1)];
}

bool ExprTokenParser::at(TokenKind kind) const {
    return !at_end() && current().kind == kind;
}

bool ExprTokenParser::at_operator(std::string_view op) const {
    return !at_end() && current().kind == TokenKind::Operator && current().text == op;
}

bool ExprTokenParser::at_identifier(std::string_view text) const {
    return !at_end() && current().kind == TokenKind::Identifier && current().text == text;
}

bool ExprTokenParser::stop_at(std::initializer_list<TokenKind> stops) const {
    if (at_end()) {
        return true;
    }
    for (const TokenKind stop : stops) {
        if (current().kind == stop) {
            return true;
        }
    }
    return false;
}

bool ExprTokenParser::match(TokenKind kind) {
    if (!at(kind)) {
        return false;
    }
    ++cursor_;
    return true;
}

bool ExprTokenParser::match_operator(std::string_view op) {
    if (!at_operator(op)) {
        return false;
    }
    ++cursor_;
    return true;
}

bool ExprTokenParser::match_identifier(std::string_view text) {
    if (!at_identifier(text)) {
        return false;
    }
    ++cursor_;
    return true;
}

SourceRange ExprTokenParser::range_between(size_t begin, size_t end) const {
    SourceRange range;
    if (begin >= tokens_.size() || begin >= end) {
        range.start = {};
        range.end = SourcePosition{};
        return range;
    }
    range.start = tokens_[begin].location;
    range.end = expr_token_end_location(tokens_[end - 1]);
    return range;
}

std::string ExprTokenParser::text_between(size_t begin, size_t end) const {
    if (begin >= tokens_.size() || begin >= end) {
        return {};
    }
    SourceLocation cursor = tokens_[begin].location;
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        const Token& token = tokens_[i];
        if (!expression_token(token)) {
            continue;
        }
        while (cursor.line < token.location.line) {
            out.push_back('\n');
            ++cursor.line;
            cursor.column = 1;
        }
        while (cursor.column < token.location.column) {
            out.push_back(' ');
            ++cursor.column;
        }
        out += token.text;
        cursor = expr_token_end_location(token);
    }
    return out;
}

Expr ExprTokenParser::make_node(ExprKind kind, size_t begin, size_t end) const {
    Expr expr;
    expr.kind = kind;
    expr.location = begin < tokens_.size() ? tokens_[begin].location : SourceLocation{};
    expr.range = range_between(begin, end);
    return expr;
}

Expr ExprTokenParser::make_node_from_start(ExprKind kind, SourceLocation start, size_t end) const {
    Expr expr;
    expr.kind = kind;
    expr.location = start;
    expr.range.start = start;
    expr.range.end =
        (end > 0 && end <= tokens_.size()) ? expr_token_end_location(tokens_[end - 1]) : start;
    return expr;
}

Expr ExprTokenParser::parse_expr_span(size_t begin, size_t end) const {
    if (begin >= end || begin >= tokens_.size()) {
        const SourceLocation location =
            begin < tokens_.size() ? tokens_[begin].location : SourceLocation{};
        return make_expr(ExprKind::Missing, "", location);
    }
    ExprTokenParser parser(tokens_.subspan(begin, end - begin));
    return parser.parse();
}

TypeRef ExprTokenParser::parse_type_span(size_t begin, size_t end) const {
    if (begin >= end || begin >= tokens_.size()) {
        const SourceLocation location =
            begin < tokens_.size() ? tokens_[begin].location : SourceLocation{};
        return make_type(TypeKind::Unknown, "", location);
    }
    TypeTokenParser parser(tokens_.subspan(begin, end - begin));
    return parser.parse();
}

std::vector<TypeRef> ExprTokenParser::parse_type_list_span(size_t begin, size_t end) const {
    if (begin >= end || begin >= tokens_.size()) {
        return {};
    }
    TypeTokenParser parser(tokens_.subspan(begin, end - begin));
    return parser.parse_list();
}

Expr ExprTokenParser::parse_comma_expr(std::initializer_list<TokenKind> stops) {
    const size_t begin = cursor_;
    Expr first = parse_named_or_binary(stops);
    if (stop_at(stops) || !match(TokenKind::Comma)) {
        return first;
    }
    std::vector<Expr> items;
    items.reserve(2);
    items.push_back(std::move(first));
    while (!stop_at(stops)) {
        if (stop_at(stops)) {
            break;
        }
        items.push_back(parse_named_or_binary(stops));
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    Expr tuple = make_node(ExprKind::TupleLiteral, begin, cursor_);
    tuple.children = std::move(items);
    return tuple;
}

Expr ExprTokenParser::parse_named_or_binary(std::initializer_list<TokenKind> stops) {
    const size_t begin = cursor_;
    if (match(TokenKind::Colon)) {
        Expr expr = make_node(ExprKind::Slice, begin, cursor_);
        expr.children.reserve(2);
        expr.children.push_back(make_expr(ExprKind::Missing, "", expr.location));
        expr.children.push_back((stop_at(stops) || at(TokenKind::Comma))
                                    ? make_expr(ExprKind::Missing, "", current().location)
                                    : parse_named_or_binary(stops));
        expr.range = range_between(begin, cursor_);
        return expr;
    }
    Expr lhs = parse_binary(1, stops);
    if (match(TokenKind::Assign) && lhs.kind == ExprKind::Name) {
        Expr expr = make_node(ExprKind::NamedArg, begin, cursor_);
        expr.name = lhs.name;
        expr.children.reserve(1);
        expr.children.push_back(parse_comma_expr(stops));
        expr.range = range_between(begin, cursor_);
        return expr;
    }
    if (!stop_at(stops) && match(TokenKind::Colon)) {
        Expr expr = make_node(ExprKind::Slice, begin, cursor_);
        expr.children.reserve(2);
        expr.children.push_back(std::move(lhs));
        expr.children.push_back((stop_at(stops) || at(TokenKind::Comma))
                                    ? make_expr(ExprKind::Missing, "", current().location)
                                    : parse_named_or_binary(stops));
        expr.range = range_between(begin, cursor_);
        return expr;
    }
    if (match_identifier("if")) {
        Expr expr = make_node(ExprKind::Conditional, begin, cursor_);
        expr.children.reserve(3);
        expr.children.push_back(std::move(lhs));
        expr.children.push_back(parse_named_or_binary(stops));
        if (match_identifier("else")) {
            expr.children.push_back(stop_at(stops)
                                        ? make_expr(ExprKind::Missing, "", current().location)
                                        : parse_named_or_binary(stops));
        }
        expr.range = range_between(begin, cursor_);
        return expr;
    }
    return lhs;
}

int ExprTokenParser::binary_precedence(const Token& token) {
    if (token.kind == TokenKind::Identifier) {
        const std::string_view text = token.text;
        if (text.size() == 2 && text[0] == 'o' && text[1] == 'r') {
            return 1;
        }
        if (text.size() == 3 && text[0] == 'a' && text[1] == 'n' && text[2] == 'd') {
            return 2;
        }
        return 0;
    }
    if (token.kind != TokenKind::Operator) {
        return 0;
    }
    const std::string_view text = token.text;
    if (text.size() == 1) {
        switch (text[0]) {
        case '<':
        case '>':
            return 3;
        case '|':
            return 4;
        case '^':
            return 5;
        case '&':
            return 6;
        case '+':
        case '-':
            return 8;
        case '*':
        case '/':
        case '%':
            return 9;
        default:
            return 0;
        }
    }
    if (text.size() == 2) {
        switch (text[0]) {
        case '=':
            return text[1] == '=' ? 3 : 0;
        case '!':
            return text[1] == '=' ? 3 : 0;
        case '<':
            return text[1] == '=' ? 3 : (text[1] == '<' ? 7 : 0);
        case '>':
            return text[1] == '=' ? 3 : (text[1] == '>' ? 7 : 0);
        default:
            return 0;
        }
    }
    return 0;
}

Expr ExprTokenParser::parse_binary(int min_precedence, std::initializer_list<TokenKind> stops) {
    const size_t begin = cursor_;
    Expr lhs = parse_prefix(stops);
    while (!stop_at(stops)) {
        const int precedence = binary_precedence(current());
        if (precedence < min_precedence) {
            break;
        }
        const std::string_view op = canonical_operator_text(current().text);
        ++cursor_;
        Expr rhs = (stop_at(stops) || at_end())
                       ? make_expr(ExprKind::Missing, "", current().location)
                       : parse_binary(precedence + 1, stops);
        Expr binary = make_node(ExprKind::Binary, begin, cursor_);
        binary.op = op;
        binary.children.reserve(2);
        binary.children.push_back(std::move(lhs));
        binary.children.push_back(std::move(rhs));
        lhs = std::move(binary);
    }
    return lhs;
}

Expr ExprTokenParser::parse_prefix(std::initializer_list<TokenKind> stops) {
    const size_t begin = cursor_;
    if (!at_end()) {
        const Token& token = current();
        if (token.kind == TokenKind::Identifier) {
            if (text_is(token.text, "def")) {
                return parse_unsupported_expr(ExprKind::DefExpression, begin, stops);
            }
            if (text_is(token.text, "lambda")) {
                ++cursor_;
                Expr expr = make_node(ExprKind::Lambda, begin, cursor_);
                expr.children.reserve(1);
                if (!stop_at(stops)) {
                    const size_t args_begin = cursor_;
                    while (!stop_at(stops) && !at(TokenKind::Colon)) {
                        ++cursor_;
                    }
                    expr.name = text_between(args_begin, cursor_);
                    if (match(TokenKind::Colon) && !stop_at(stops)) {
                        expr.children.push_back(parse_named_or_binary(stops));
                    }
                }
                expr.range = range_between(begin, cursor_);
                return expr;
            }
            if (text_is(token.text, "not")) {
                ++cursor_;
                return parse_unary("not", begin, stops);
            }
            if (text_is(token.text, "await")) {
                ++cursor_;
                Expr expr = make_node(ExprKind::Await, begin, cursor_);
                expr.children.reserve(1);
                expr.children.push_back(parse_prefix(stops));
                expr.range = range_between(begin, cursor_);
                return expr;
            }
            if (text_is(token.text, "yield")) {
                ++cursor_;
                Expr expr = make_node(ExprKind::Yield, begin, cursor_);
                expr.children.reserve(1);
                expr.children.push_back(parse_prefix(stops));
                expr.range = range_between(begin, cursor_);
                return expr;
            }
        } else if (token.kind == TokenKind::Operator) {
            if (text_is(token.text, "*") && pointer_cast_call_ahead()) {
                return parse_pointer_cast_call();
            }
            if (text_is(token.text, "*") || text_is(token.text, "-") || text_is(token.text, "&") ||
                text_is(token.text, "~")) {
                ++cursor_;
                return parse_unary(canonical_operator_text(token.text), begin, stops);
            }
        }
    }
    return parse_postfix(stops);
}

Expr ExprTokenParser::parse_unsupported_expr(ExprKind kind, size_t begin,
                                             std::initializer_list<TokenKind> stops) {
    int depth = 0;
    while (!at_end()) {
        if (depth == 0) {
            bool should_stop = false;
            for (const TokenKind stop : stops) {
                if (current().kind == stop) {
                    should_stop = true;
                    break;
                }
            }
            if (should_stop) {
                break;
            }
        }
        if (current().kind == TokenKind::LParen || current().kind == TokenKind::LBracket ||
            current().kind == TokenKind::LBrace) {
            ++depth;
        } else if ((current().kind == TokenKind::RParen || current().kind == TokenKind::RBracket ||
                    current().kind == TokenKind::RBrace) &&
                   depth > 0) {
            --depth;
        }
        ++cursor_;
    }
    return make_node(kind, begin, cursor_);
}

Expr ExprTokenParser::parse_unary(std::string_view op, size_t begin,
                                  std::initializer_list<TokenKind> stops) {
    Expr expr = make_node(ExprKind::Unary, begin, cursor_);
    expr.op = op;
    expr.children.reserve(1);
    if (stop_at(stops) || at_end()) {
        expr.children.push_back(make_expr(ExprKind::Missing, "", current().location));
    } else {
        expr.children.push_back(parse_prefix(stops));
    }
    expr.range = range_between(begin, cursor_);
    return expr;
}

} // namespace dudu
