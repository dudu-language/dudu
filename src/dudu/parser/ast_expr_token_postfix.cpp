#include "dudu/core/ast_expr.hpp"
#include "dudu/parser/ast_expr_token_parser.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_lower.hpp"

#include <algorithm>
#include <utility>

namespace dudu {
namespace {

std::string string_literal_value(std::string_view text) {
    if (text.size() >= 6 && text[0] == text[1] && text[1] == text[2] &&
        text[text.size() - 1] == text[0] && text[text.size() - 2] == text[0] &&
        text[text.size() - 3] == text[0]) {
        return std::string(text.substr(3, text.size() - 6));
    }
    if (text.size() >= 2) {
        return std::string(text.substr(1, text.size() - 2));
    }
    return {};
}

} // namespace

Expr ExprTokenParser::parse_postfix(std::initializer_list<TokenKind> stops) {
    Expr expr = parse_primary(stops);
    while (!stop_at(stops)) {
        if (match(TokenKind::Dot)) {
            if (!at(TokenKind::Identifier)) {
                break;
            }
            const SourceLocation start = expr.range.start;
            const Token& name = current();
            ++cursor_;
            Expr member = make_node_from_start(ExprKind::Member, start, cursor_);
            member.name = name.text;
            member.children.reserve(1);
            member.children.push_back(std::move(expr));
            expr = std::move(member);
            continue;
        }
        if (match(TokenKind::LBracket)) {
            const SourceLocation start = expr.range.start;
            const size_t body_begin = cursor_;
            const size_t close =
                matching_close(body_begin - 1, TokenKind::LBracket, TokenKind::RBracket);
            if (close < tokens_.size() && close + 1 < tokens_.size() &&
                tokens_[close + 1].kind == TokenKind::LParen) {
                cursor_ = close + 1;
                expr = parse_template_call_from_brackets(std::move(expr), start, body_begin, close,
                                                         stops);
                continue;
            }
            Expr index_arg = parse_index_argument();
            match(TokenKind::RBracket);
            Expr index = make_node_from_start(ExprKind::Index, start, cursor_);
            index.children.reserve(2);
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

size_t ExprTokenParser::matching_close(size_t open, TokenKind open_kind,
                                       TokenKind close_kind) const {
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

bool ExprTokenParser::span_has_top_level_identifier(size_t begin, size_t end,
                                                    std::string_view text) const {
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    for (size_t i = begin; i < end && i < tokens_.size(); ++i) {
        const Token& token = tokens_[i];
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (!inside_group && token.kind == TokenKind::Identifier && token.text == text) {
            return true;
        }
        if (token.kind == TokenKind::LBracket) {
            ++bracket_depth;
        } else if (token.kind == TokenKind::RBracket && bracket_depth > 0) {
            --bracket_depth;
        } else if (token.kind == TokenKind::LParen) {
            ++paren_depth;
        } else if (token.kind == TokenKind::RParen && paren_depth > 0) {
            --paren_depth;
        } else if (token.kind == TokenKind::LBrace) {
            ++brace_depth;
        } else if (token.kind == TokenKind::RBrace && brace_depth > 0) {
            --brace_depth;
        }
    }
    return false;
}

size_t ExprTokenParser::expr_token_begin(const Expr& expr) const {
    for (size_t i = 0; i < tokens_.size(); ++i) {
        if (tokens_[i].location.line == expr.range.start.line &&
            tokens_[i].location.column == expr.range.start.column) {
            return i;
        }
    }
    return cursor_;
}

size_t ExprTokenParser::expr_token_end(const Expr& expr) const {
    const size_t begin = expr_token_begin(expr);
    for (size_t i = begin; i < tokens_.size(); ++i) {
        const SourceLocation end = expr_token_end_location(tokens_[i]);
        if (end.line == expr.range.end.line && end.column == expr.range.end.column) {
            return i + 1;
        }
    }
    return begin;
}

Expr ExprTokenParser::parse_call(Expr callee, std::initializer_list<TokenKind> stops) {
    (void)stops;
    const SourceLocation start = callee.range.start;
    const size_t open = cursor_;
    match(TokenKind::LParen);
    const size_t body_begin = cursor_;
    const size_t close = matching_close(open, TokenKind::LParen, TokenKind::RParen);
    if (close < tokens_.size() && span_has_top_level_identifier(body_begin, close, "for")) {
        cursor_ = close + 1;
        return make_node_from_start(ExprKind::Comprehension, start, cursor_);
    }
    std::vector<Expr> args = parse_arg_list(TokenKind::RParen);
    match(TokenKind::RParen);
    Expr call = make_node_from_start(ExprKind::Call, start, cursor_);
    const std::optional<ExprPath> path = expr_path_from_expr(callee);
    const std::string callee_name = path ? render_expr_path(*path) : display_expr(callee);
    set_expr_callee(call, std::vector<Expr>{std::move(callee)});
    call.children = std::move(args);
    if (callee_name == "cpp") {
        call.kind = ExprKind::CppEscape;
        if (call.children.size() == 1 && call.children.front().kind == ExprKind::StringLiteral) {
            call.value = call.children.front().value;
        }
    }
    return call;
}

Expr ExprTokenParser::parse_template_call(Expr indexed_callee,
                                          std::initializer_list<TokenKind> stops) {
    (void)stops;
    const size_t begin = expr_token_begin(indexed_callee);
    Expr callee = std::move(indexed_callee.children[0]);
    Expr template_expr = std::move(indexed_callee.children[1]);
    const size_t template_begin = expr_token_begin(template_expr);
    const size_t template_end = expr_token_end(template_expr);
    match(TokenKind::LParen);
    std::vector<Expr> args = parse_arg_list(TokenKind::RParen);
    match(TokenKind::RParen);
    Expr call = make_node(ExprKind::TemplateCall, begin, cursor_);
    set_expr_callee(call, std::vector<Expr>{std::move(callee)});
    if (expr_missing(template_expr)) {
        set_expr_template_args(call, {});
    } else if (template_expr.kind == ExprKind::TupleLiteral) {
        set_expr_template_args(call, std::move(template_expr.children));
    } else {
        set_expr_template_args(call, std::vector<Expr>{std::move(template_expr)});
    }
    set_expr_template_type_args(call, parse_type_list_span(template_begin, template_end));
    call.children = std::move(args);
    return call;
}

Expr ExprTokenParser::parse_template_call_from_brackets(Expr callee, SourceLocation start,
                                                        size_t template_begin, size_t template_end,
                                                        std::initializer_list<TokenKind> stops) {
    (void)stops;
    match(TokenKind::LParen);
    std::vector<Expr> args = parse_arg_list(TokenKind::RParen);
    match(TokenKind::RParen);

    Expr call = make_node_from_start(ExprKind::TemplateCall, start, cursor_);
    set_expr_callee(call, std::vector<Expr>{std::move(callee)});
    set_expr_template_type_args(call, parse_type_list_span(template_begin, template_end));
    Expr template_expr = parse_expr_span(template_begin, template_end);
    if (!expr_missing(template_expr) && template_expr.kind != ExprKind::Unknown) {
        if (template_expr.kind == ExprKind::TupleLiteral) {
            set_expr_template_args(call, std::move(template_expr.children));
        } else {
            set_expr_template_args(call, std::vector<Expr>{std::move(template_expr)});
        }
    }
    call.children = std::move(args);
    return call;
}

std::vector<Expr> ExprTokenParser::parse_arg_list(TokenKind close) {
    std::vector<Expr> args;
    while (!at(close) && !at_end()) {
        args.push_back(parse_named_or_binary({TokenKind::Comma, close}));
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    return args;
}

Expr ExprTokenParser::parse_index_argument() {
    if (at(TokenKind::RBracket)) {
        return make_expr(ExprKind::Missing, "", current().location);
    }
    const size_t begin = cursor_;
    std::vector<Expr> items;
    while (!at(TokenKind::RBracket) && !at_end()) {
        items.push_back(parse_index_item());
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    if (items.size() == 1) {
        return std::move(items.front());
    }
    Expr tuple = make_node(ExprKind::TupleLiteral, begin, cursor_);
    tuple.children = std::move(items);
    return tuple;
}

Expr ExprTokenParser::parse_index_item() {
    const size_t begin = cursor_;
    if (match(TokenKind::Ellipsis)) {
        return make_node(ExprKind::Ellipsis, begin, cursor_);
    }
    if (match_identifier("None")) {
        return make_node(ExprKind::NewAxis, begin, cursor_);
    }
    return parse_named_or_binary({TokenKind::Comma, TokenKind::RBracket});
}

Expr ExprTokenParser::parse_primary(std::initializer_list<TokenKind> stops) {
    (void)stops;
    const size_t begin = cursor_;
    if (at(TokenKind::Number)) {
        const Token& token = current();
        ++cursor_;
        Expr expr =
            make_node(is_float_literal(token.text) ? ExprKind::FloatLiteral : ExprKind::IntLiteral,
                      begin, cursor_);
        expr.value = token.text;
        return expr;
    }
    if (at(TokenKind::String)) {
        const Token& token = current();
        ++cursor_;
        Expr expr = make_node(ExprKind::StringLiteral, begin, cursor_);
        expr.value = string_literal_value(token.text);
        return expr;
    }
    if (at_identifier("True") || at_identifier("False")) {
        const Token& token = current();
        ++cursor_;
        Expr expr = make_node(ExprKind::BoolLiteral, begin, cursor_);
        expr.value = token.text;
        return expr;
    }
    if (match_identifier("None")) {
        return make_node(ExprKind::NoneLiteral, begin, cursor_);
    }
    if (match(TokenKind::Ellipsis)) {
        return make_node(ExprKind::Ellipsis, begin, cursor_);
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
        inner.location = tokens_[begin].location;
        inner.range = range_between(begin, cursor_);
        return inner;
    }
    if (match(TokenKind::LBracket)) {
        const size_t body_begin = cursor_;
        const size_t close = matching_close(begin, TokenKind::LBracket, TokenKind::RBracket);
        if (close < tokens_.size() && span_has_top_level_identifier(body_begin, close, "for")) {
            cursor_ = close + 1;
            return make_node(ExprKind::Comprehension, begin, cursor_);
        }
        Expr list = make_node(ExprKind::ListLiteral, begin, cursor_);
        list.children = parse_arg_list(TokenKind::RBracket);
        match(TokenKind::RBracket);
        list.range = range_between(begin, cursor_);
        return list;
    }
    if (match(TokenKind::LBrace)) {
        const size_t body_begin = cursor_;
        const size_t close = matching_close(begin, TokenKind::LBrace, TokenKind::RBrace);
        if (close < tokens_.size() && span_has_top_level_identifier(body_begin, close, "for")) {
            cursor_ = close + 1;
            return make_node(ExprKind::Comprehension, begin, cursor_);
        }
        return parse_brace_literal(begin);
    }
    return make_node(ExprKind::Unknown, begin, std::min(cursor_ + 1, tokens_.size()));
}

Expr ExprTokenParser::parse_brace_literal(size_t begin) {
    std::vector<Expr> entries;
    bool dict = false;
    while (!at(TokenKind::RBrace) && !at_end()) {
        const size_t entry_begin = cursor_;
        Expr key = parse_named_or_binary({TokenKind::Colon, TokenKind::Comma, TokenKind::RBrace});
        if (match(TokenKind::Colon)) {
            dict = true;
            Expr entry = make_node(ExprKind::DictEntry, entry_begin, cursor_);
            entry.children.reserve(2);
            entry.children.push_back(std::move(key));
            entry.children.push_back(parse_named_or_binary({TokenKind::Comma, TokenKind::RBrace}));
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
    Expr literal = make_node(dict ? ExprKind::DictLiteral : ExprKind::SetLiteral, begin, cursor_);
    literal.children = std::move(entries);
    return literal;
}

bool ExprTokenParser::pointer_cast_call_ahead() const {
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
        if (depth == 0 && (token.kind == TokenKind::Operator || token.kind == TokenKind::Comma ||
                           token.kind == TokenKind::Colon)) {
            return false;
        }
    }
    return false;
}

Expr ExprTokenParser::parse_pointer_cast_call() {
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
    if (!match(TokenKind::LParen)) {
        Expr expr = make_node(ExprKind::Unary, begin, cursor_);
        expr.op = "*";
        return expr;
    }
    std::vector<Expr> args = parse_arg_list(TokenKind::RParen);
    match(TokenKind::RParen);

    Expr callee = make_node(ExprKind::Name, begin, type_end);
    callee.name = "*";
    Expr call = make_node(ExprKind::Call, begin, cursor_);
    set_expr_callee(call, std::vector<Expr>{std::move(callee)});
    call.children = std::move(args);
    set_expr_type_ref(call, parse_type_span(type_begin, type_end));
    return call;
}

} // namespace dudu
