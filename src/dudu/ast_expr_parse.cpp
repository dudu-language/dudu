#include "dudu/ast_expr.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/lexer.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

SourceLocation token_end_location(const Token& token) {
    SourceLocation end = token.location;
    end.column += static_cast<int>(token.text.size());
    return end;
}

void shift_token_locations(std::vector<Token>& tokens, const SourceLocation& base) {
    for (Token& token : tokens) {
        token.location.file = base.file;
        token.location.line += base.line - 1;
        if (token.location.line == base.line) {
            token.location.column += base.column - 1;
        }
    }
}

bool expression_token(const Token& token) {
    return token.kind != TokenKind::Newline && token.kind != TokenKind::Indent &&
           token.kind != TokenKind::Dedent && token.kind != TokenKind::End;
}

class ExprTokenParser {
  public:
    explicit ExprTokenParser(std::span<const Token> tokens) : tokens_(tokens) {
    }

    Expr parse() {
        if (at_end()) {
            return make_expr(ExprKind::Unknown, "", {});
        }
        const size_t begin = cursor_;
        Expr expr = parse_comma_expr({});
        if (!at_end()) {
            return make_node(ExprKind::Unknown, begin, tokens_.size() - 1);
        }
        expr.text = text_between(begin, cursor_);
        expr.range = range_between(begin, cursor_);
        return expr;
    }

  private:
    std::span<const Token> tokens_;
    size_t cursor_ = 0;

    bool at_end() const {
        return cursor_ >= tokens_.size() || !expression_token(tokens_[cursor_]);
    }

    const Token& current() const {
        return tokens_[std::min(cursor_, tokens_.size() - 1)];
    }

    bool at(TokenKind kind) const {
        return !at_end() && current().kind == kind;
    }

    bool at_operator(std::string_view op) const {
        return !at_end() && current().kind == TokenKind::Operator && current().text == op;
    }

    bool at_identifier(std::string_view text) const {
        return !at_end() && current().kind == TokenKind::Identifier && current().text == text;
    }

    bool stop_at(std::initializer_list<TokenKind> stops) const {
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

    bool match(TokenKind kind) {
        if (!at(kind)) {
            return false;
        }
        ++cursor_;
        return true;
    }

    bool match_operator(std::string_view op) {
        if (!at_operator(op)) {
            return false;
        }
        ++cursor_;
        return true;
    }

    bool match_identifier(std::string_view text) {
        if (!at_identifier(text)) {
            return false;
        }
        ++cursor_;
        return true;
    }

    SourceRange range_between(size_t begin, size_t end) const {
        SourceRange range;
        if (begin >= tokens_.size() || begin >= end) {
            range.start = {};
            range.end = {};
            return range;
        }
        range.start = tokens_[begin].location;
        range.end = token_end_location(tokens_[end - 1]);
        return range;
    }

    std::string text_between(size_t begin, size_t end) const {
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
            cursor = token_end_location(token);
        }
        return out;
    }

    Expr make_node(ExprKind kind, size_t begin, size_t end) const {
        Expr expr;
        expr.kind = kind;
        expr.text = text_between(begin, end);
        expr.location = begin < tokens_.size() ? tokens_[begin].location : SourceLocation{};
        expr.range = range_between(begin, end);
        return expr;
    }

    Expr parse_comma_expr(std::initializer_list<TokenKind> stops) {
        const size_t begin = cursor_;
        std::vector<Expr> items;
        items.push_back(parse_named_or_binary(stops));
        while (!stop_at(stops) && match(TokenKind::Comma)) {
            if (stop_at(stops)) {
                break;
            }
            items.push_back(parse_named_or_binary(stops));
        }
        if (items.size() == 1) {
            return std::move(items.front());
        }
        Expr tuple = make_node(ExprKind::TupleLiteral, begin, cursor_);
        tuple.children = std::move(items);
        return tuple;
    }

    Expr parse_named_or_binary(std::initializer_list<TokenKind> stops) {
        const size_t begin = cursor_;
        if (match(TokenKind::Colon)) {
            Expr expr = make_node(ExprKind::Slice, begin, cursor_);
            expr.children.push_back(make_expr(ExprKind::Unknown, "", expr.location));
            expr.children.push_back((stop_at(stops) || at(TokenKind::Comma))
                                        ? make_expr(ExprKind::Unknown, "", current().location)
                                        : parse_named_or_binary(stops));
            expr.text = text_between(begin, cursor_);
            expr.range = range_between(begin, cursor_);
            return expr;
        }
        Expr lhs = parse_binary(1, stops);
        if (match(TokenKind::Assign) && lhs.kind == ExprKind::Name) {
            Expr expr = make_node(ExprKind::NamedArg, begin, cursor_);
            expr.name = lhs.name;
            expr.children.push_back(parse_comma_expr(stops));
            expr.text = text_between(begin, cursor_);
            expr.range = range_between(begin, cursor_);
            return expr;
        }
        if (!stop_at(stops) && match(TokenKind::Colon)) {
            Expr expr = make_node(ExprKind::Slice, begin, cursor_);
            expr.children.push_back(std::move(lhs));
            expr.children.push_back((stop_at(stops) || at(TokenKind::Comma))
                                        ? make_expr(ExprKind::Unknown, "", current().location)
                                        : parse_named_or_binary(stops));
            expr.text = text_between(begin, cursor_);
            expr.range = range_between(begin, cursor_);
            return expr;
        }
        if (match_identifier("if")) {
            Expr expr = make_node(ExprKind::Conditional, begin, cursor_);
            expr.children.push_back(std::move(lhs));
            expr.children.push_back(parse_named_or_binary(stops));
            if (match_identifier("else")) {
                expr.children.push_back(stop_at(stops)
                                            ? make_expr(ExprKind::Unknown, "", current().location)
                                            : parse_named_or_binary(stops));
            }
            expr.text = text_between(begin, cursor_);
            expr.range = range_between(begin, cursor_);
            return expr;
        }
        return lhs;
    }

    static int binary_precedence(const Token& token) {
        if (token.kind == TokenKind::Identifier) {
            if (token.text == "or") {
                return 1;
            }
            if (token.text == "and") {
                return 2;
            }
            return 0;
        }
        if (token.kind != TokenKind::Operator) {
            return 0;
        }
        if (token.text == "==" || token.text == "!=" || token.text == "<=" || token.text == ">=" ||
            token.text == "<" || token.text == ">") {
            return 3;
        }
        if (token.text == "|") {
            return 4;
        }
        if (token.text == "^") {
            return 5;
        }
        if (token.text == "&") {
            return 6;
        }
        if (token.text == "<<" || token.text == ">>") {
            return 7;
        }
        if (token.text == "+" || token.text == "-") {
            return 8;
        }
        if (token.text == "*" || token.text == "/" || token.text == "%") {
            return 9;
        }
        return 0;
    }

    Expr parse_binary(int min_precedence, std::initializer_list<TokenKind> stops) {
        const size_t begin = cursor_;
        Expr lhs = parse_prefix(stops);
        while (!stop_at(stops)) {
            const int precedence = binary_precedence(current());
            if (precedence < min_precedence) {
                break;
            }
            const std::string op = current().text;
            ++cursor_;
            Expr rhs = parse_binary(precedence + 1, stops);
            Expr binary = make_node(ExprKind::Binary, begin, cursor_);
            binary.op = op;
            binary.children.push_back(std::move(lhs));
            binary.children.push_back(std::move(rhs));
            lhs = std::move(binary);
        }
        return lhs;
    }

    Expr parse_prefix(std::initializer_list<TokenKind> stops) {
        const size_t begin = cursor_;
        if (at_identifier("def")) {
            return parse_unknown_until_stops(begin, stops);
        }
        if (match_identifier("lambda")) {
            Expr expr = make_node(ExprKind::Lambda, begin, cursor_);
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
            expr.text = text_between(begin, cursor_);
            expr.range = range_between(begin, cursor_);
            return expr;
        }
        if (match_identifier("not")) {
            return parse_unary("not", begin, stops);
        }
        if (match_identifier("await")) {
            Expr expr = make_node(ExprKind::Await, begin, cursor_);
            expr.children.push_back(parse_prefix(stops));
            expr.text = text_between(begin, cursor_);
            expr.range = range_between(begin, cursor_);
            return expr;
        }
        if (match_identifier("yield")) {
            Expr expr = make_node(ExprKind::Yield, begin, cursor_);
            expr.children.push_back(parse_prefix(stops));
            expr.text = text_between(begin, cursor_);
            expr.range = range_between(begin, cursor_);
            return expr;
        }
        if (at_operator("*") && pointer_cast_call_ahead()) {
            return parse_pointer_cast_call();
        }
        if (match_operator("*")) {
            return parse_unary("*", begin, stops);
        }
        if (match_operator("-")) {
            return parse_unary("-", begin, stops);
        }
        if (match_operator("&")) {
            return parse_unary("&", begin, stops);
        }
        if (match_operator("~")) {
            return parse_unary("~", begin, stops);
        }
        return parse_postfix(stops);
    }

    Expr parse_unknown_until_stops(size_t begin, std::initializer_list<TokenKind> stops) {
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
            } else if ((current().kind == TokenKind::RParen ||
                        current().kind == TokenKind::RBracket ||
                        current().kind == TokenKind::RBrace) &&
                       depth > 0) {
                --depth;
            }
            ++cursor_;
        }
        return make_node(ExprKind::Unknown, begin, cursor_);
    }

    Expr parse_unary(std::string op, size_t begin, std::initializer_list<TokenKind> stops) {
        Expr expr = make_node(ExprKind::Unary, begin, cursor_);
        expr.op = std::move(op);
        expr.children.push_back(parse_prefix(stops));
        expr.text = text_between(begin, cursor_);
        expr.range = range_between(begin, cursor_);
        return expr;
    }

    Expr parse_postfix(std::initializer_list<TokenKind> stops) {
        Expr expr = parse_primary(stops);
        while (!stop_at(stops)) {
            if (match(TokenKind::Dot)) {
                if (!at(TokenKind::Identifier)) {
                    break;
                }
                const size_t begin = expr_token_begin(expr);
                const Token& name = current();
                ++cursor_;
                Expr member = make_node(ExprKind::Member, begin, cursor_);
                member.name = name.text;
                member.children.push_back(std::move(expr));
                expr = std::move(member);
                continue;
            }
            if (match(TokenKind::LBracket)) {
                const size_t begin = expr_token_begin(expr);
                const size_t body_begin = cursor_;
                const size_t close =
                    matching_close(body_begin - 1, TokenKind::LBracket, TokenKind::RBracket);
                if (close < tokens_.size() && close + 1 < tokens_.size() &&
                    tokens_[close + 1].kind == TokenKind::LParen) {
                    const std::string template_text = text_between(body_begin, close);
                    const SourceLocation template_location =
                        body_begin < tokens_.size() ? tokens_[body_begin].location : expr.range.end;
                    cursor_ = close + 1;
                    expr = parse_template_call_from_brackets(std::move(expr), begin, template_text,
                                                             template_location, stops);
                    continue;
                }
                Expr index_arg = parse_index_argument();
                match(TokenKind::RBracket);
                Expr index = make_node(ExprKind::Index, begin, cursor_);
                index.children.push_back(std::move(expr));
                index.children.push_back(std::move(index_arg));
                expr = std::move(index);
                continue;
            }
            if (at(TokenKind::LParen)) {
                if (expr.kind == ExprKind::Index && expr.children.size() == 2) {
                    expr = parse_template_call(std::move(expr), stops);
                } else {
                    expr = parse_call(std::move(expr), stops);
                }
                continue;
            }
            break;
        }
        return expr;
    }

    size_t matching_close(size_t open, TokenKind open_kind, TokenKind close_kind) const {
        int depth = 0;
        for (size_t i = open; i < tokens_.size(); ++i) {
            if (tokens_[i].kind == open_kind) {
                ++depth;
            } else if (tokens_[i].kind == close_kind) {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return tokens_.size();
    }

    size_t expr_token_begin(const Expr& expr) const {
        for (size_t i = 0; i < tokens_.size(); ++i) {
            if (tokens_[i].location.line == expr.range.start.line &&
                tokens_[i].location.column == expr.range.start.column) {
                return i;
            }
        }
        return cursor_;
    }

    Expr parse_call(Expr callee, std::initializer_list<TokenKind> stops) {
        (void)stops;
        const size_t begin = expr_token_begin(callee);
        match(TokenKind::LParen);
        std::vector<Expr> args = parse_arg_list(TokenKind::RParen);
        match(TokenKind::RParen);
        Expr call = make_node(ExprKind::Call, begin, cursor_);
        call.name = callee.text;
        call.callee.push_back(std::move(callee));
        call.children = std::move(args);
        if (call.name == "cpp") {
            call.kind = ExprKind::CppEscape;
            call.value = cpp_escape_body(call.text);
        }
        return call;
    }

    Expr parse_template_call(Expr indexed_callee, std::initializer_list<TokenKind> stops) {
        (void)stops;
        const size_t begin = expr_token_begin(indexed_callee);
        Expr callee = std::move(indexed_callee.children[0]);
        Expr template_expr = std::move(indexed_callee.children[1]);
        const std::string template_text = template_expr.text;
        const SourceLocation template_location = template_expr.location;
        match(TokenKind::LParen);
        std::vector<Expr> args = parse_arg_list(TokenKind::RParen);
        match(TokenKind::RParen);
        Expr call = make_node(ExprKind::TemplateCall, begin, cursor_);
        call.name = callee.text;
        call.callee.push_back(std::move(callee));
        if (expr_missing(template_expr)) {
            call.template_args.clear();
        } else if (template_expr.kind == ExprKind::TupleLiteral) {
            call.template_args = std::move(template_expr.children);
        } else {
            call.template_args.push_back(std::move(template_expr));
        }
        call.template_type_args = parse_type_list(template_text, template_location);
        call.children = std::move(args);
        return call;
    }

    Expr parse_template_call_from_brackets(Expr callee, size_t begin,
                                           const std::string& template_text,
                                           SourceLocation template_location,
                                           std::initializer_list<TokenKind> stops) {
        (void)stops;
        match(TokenKind::LParen);
        std::vector<Expr> args = parse_arg_list(TokenKind::RParen);
        match(TokenKind::RParen);

        Expr call = make_node(ExprKind::TemplateCall, begin, cursor_);
        call.name = callee.text;
        call.callee.push_back(std::move(callee));
        call.template_type_args = parse_type_list(template_text, template_location);
        Expr template_expr = parse_expr_text(template_text, template_location);
        if (!expr_missing(template_expr) && template_expr.kind != ExprKind::Unknown) {
            if (template_expr.kind == ExprKind::TupleLiteral) {
                call.template_args = std::move(template_expr.children);
            } else {
                call.template_args.push_back(std::move(template_expr));
            }
        }
        call.children = std::move(args);
        return call;
    }

    std::vector<Expr> parse_arg_list(TokenKind close) {
        std::vector<Expr> args;
        while (!at(close) && !at_end()) {
            args.push_back(parse_named_or_binary({TokenKind::Comma, close}));
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        return args;
    }

    Expr parse_index_argument() {
        if (at(TokenKind::RBracket)) {
            return make_expr(ExprKind::Unknown, "", current().location);
        }
        return parse_comma_expr({TokenKind::RBracket});
    }

    Expr parse_primary(std::initializer_list<TokenKind> stops) {
        (void)stops;
        const size_t begin = cursor_;
        if (at(TokenKind::Number)) {
            const Token& token = current();
            ++cursor_;
            Expr expr = make_node(is_float_literal(token.text) ? ExprKind::FloatLiteral
                                                               : ExprKind::IntLiteral,
                                  begin, cursor_);
            expr.value = token.text;
            return expr;
        }
        if (at(TokenKind::String)) {
            const Token& token = current();
            ++cursor_;
            Expr expr = make_node(ExprKind::StringLiteral, begin, cursor_);
            if (token.text.size() >= 2) {
                expr.value = token.text.substr(1, token.text.size() - 2);
            }
            return expr;
        }
        if (match_identifier("True") || match_identifier("False")) {
            Expr expr = make_node(ExprKind::BoolLiteral, begin, cursor_);
            expr.value = expr.text;
            return expr;
        }
        if (match_identifier("None")) {
            return make_node(ExprKind::NoneLiteral, begin, cursor_);
        }
        if (at(TokenKind::Identifier)) {
            const Token& token = current();
            ++cursor_;
            Expr expr = make_node(ExprKind::Name, begin, cursor_);
            expr.name = token.text;
            return expr;
        }
        if (match(TokenKind::LParen)) {
            if (at(TokenKind::RParen)) {
                match(TokenKind::RParen);
                return make_node(ExprKind::TupleLiteral, begin, cursor_);
            }
            Expr inner = parse_comma_expr({TokenKind::RParen});
            match(TokenKind::RParen);
            inner.text = text_between(begin, cursor_);
            inner.location = tokens_[begin].location;
            inner.range = range_between(begin, cursor_);
            return inner;
        }
        if (match(TokenKind::LBracket)) {
            Expr list = make_node(ExprKind::ListLiteral, begin, cursor_);
            list.children = parse_arg_list(TokenKind::RBracket);
            match(TokenKind::RBracket);
            list.text = text_between(begin, cursor_);
            list.range = range_between(begin, cursor_);
            return list;
        }
        if (match(TokenKind::LBrace)) {
            return parse_brace_literal(begin);
        }
        return make_node(ExprKind::Unknown, begin, std::min(cursor_ + 1, tokens_.size()));
    }

    Expr parse_brace_literal(size_t begin) {
        std::vector<Expr> entries;
        bool dict = false;
        while (!at(TokenKind::RBrace) && !at_end()) {
            const size_t entry_begin = cursor_;
            Expr key =
                parse_named_or_binary({TokenKind::Colon, TokenKind::Comma, TokenKind::RBrace});
            if (match(TokenKind::Colon)) {
                dict = true;
                Expr entry = make_node(ExprKind::DictEntry, entry_begin, cursor_);
                entry.children.push_back(std::move(key));
                entry.children.push_back(
                    parse_named_or_binary({TokenKind::Comma, TokenKind::RBrace}));
                entry.text = text_between(entry_begin, cursor_);
                entry.range = range_between(entry_begin, cursor_);
                entries.push_back(std::move(entry));
            } else {
                entries.push_back(std::move(key));
            }
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        match(TokenKind::RBrace);
        Expr literal =
            make_node(dict ? ExprKind::DictLiteral : ExprKind::SetLiteral, begin, cursor_);
        literal.children = std::move(entries);
        return literal;
    }

    bool pointer_cast_call_ahead() const {
        size_t depth = 0;
        for (size_t i = cursor_ + 1; i < tokens_.size(); ++i) {
            const Token& token = tokens_[i];
            if (!expression_token(token)) {
                return false;
            }
            if (token.kind == TokenKind::LBracket) {
                ++depth;
                continue;
            }
            if (token.kind == TokenKind::RBracket && depth > 0) {
                --depth;
                continue;
            }
            if (depth == 0 && token.kind == TokenKind::LParen) {
                return i > cursor_ + 1;
            }
            if (depth == 0 && (token.kind == TokenKind::Operator ||
                               token.kind == TokenKind::Comma || token.kind == TokenKind::Colon)) {
                return false;
            }
        }
        return false;
    }

    Expr parse_pointer_cast_call() {
        const size_t begin = cursor_;
        ++cursor_; // *
        size_t type_begin = cursor_;
        size_t depth = 0;
        while (!at_end()) {
            if (at(TokenKind::LBracket)) {
                ++depth;
            } else if (at(TokenKind::RBracket) && depth > 0) {
                --depth;
            } else if (depth == 0 && at(TokenKind::LParen)) {
                break;
            }
            ++cursor_;
        }
        const size_t type_end = cursor_;
        std::string callee_text = text_between(begin, type_end);
        if (!match(TokenKind::LParen)) {
            Expr expr = make_node(ExprKind::Unary, begin, cursor_);
            expr.op = "*";
            return expr;
        }
        std::vector<Expr> args = parse_arg_list(TokenKind::RParen);
        match(TokenKind::RParen);

        Expr callee = make_node(ExprKind::Name, begin, type_end);
        callee.name = callee_text;
        Expr call = make_node(ExprKind::Call, begin, cursor_);
        call.name = callee_text;
        call.callee.push_back(std::move(callee));
        call.children = std::move(args);

        const std::string type_text = text_between(type_begin, type_end);
        const size_t bracket = type_text.find('[');
        if (bracket != std::string::npos && type_text.ends_with("]")) {
            call.kind = ExprKind::TemplateCall;
            call.name = "*" + trim_string(type_text.substr(0, bracket));
            const std::string args_text =
                type_text.substr(bracket + 1, type_text.size() - bracket - 2);
            SourceLocation args_location = tokens_[type_begin].location;
            args_location.column += static_cast<int>(bracket + 1);
            call.template_args.push_back(parse_expr_text(args_text, args_location));
            call.template_type_args = parse_type_list(args_text, args_location);
        }
        return call;
    }
};

} // namespace

Expr parse_expr_text(std::string_view text, SourceLocation location) {
    text = trim_view_with_location(text, location);
    if (text.empty()) {
        return make_expr(ExprKind::Unknown, text, location);
    }
    std::vector<Token> tokens = lex_source(text, location.file);
    shift_token_locations(tokens, location);
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                [](const Token& token) {
                                    return token.kind == TokenKind::Newline ||
                                           token.kind == TokenKind::Indent ||
                                           token.kind == TokenKind::Dedent;
                                }),
                 tokens.end());
    ExprTokenParser parser(tokens);
    return parser.parse();
}

} // namespace dudu
