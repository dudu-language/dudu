#include "dudu/parser/ast_type_token_parser.hpp"

#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/core/ast_type.hpp"

#include <algorithm>
#include <utility>

namespace dudu {
namespace {

bool type_token(const Token& token) {
    return token.kind != TokenKind::Newline && token.kind != TokenKind::Indent &&
           token.kind != TokenKind::Dedent && token.kind != TokenKind::End;
}

SourceLocation token_end_location(const Token& token) {
    SourceLocation end = token.location;
    end.column += static_cast<int>(token.text.size());
    return end;
}

TypeRef shape_dim_ref(TypeRef dim) {
    if (dim.kind == TypeKind::Named && dim.name == "dyn") {
        dim.kind = TypeKind::Value;
        dim.value = "dyn";
        dim.name = {};
    }
    return dim;
}

std::vector<TypeRef> shape_dim_refs(std::vector<TypeRef> dims) {
    for (TypeRef& dim : dims) {
        dim = shape_dim_ref(std::move(dim));
    }
    return dims;
}

bool type_metadata_base_args_are_types(const TypeRef& type) {
    if (type.kind != TypeKind::Template) {
        return false;
    }
    for (const TypeRef& child : type.children) {
        if (child.kind == TypeKind::Value || child.kind == TypeKind::Unknown) {
            return false;
        }
    }
    return true;
}

} // namespace

TypeTokenParser::TypeTokenParser(std::span<const Token> tokens) : tokens_(tokens) {
}

TypeRef TypeTokenParser::parse() {
    if (at_end()) {
        return make_type(TypeKind::Unknown, "", {});
    }
    const size_t begin = cursor_;
    TypeRef type = parse_type({});
    if (!at_end()) {
        return make_node(TypeKind::Unknown, begin, tokens_.size() - 1);
    }
    type.range = range_between(begin, cursor_);
    return type;
}

std::vector<TypeRef> TypeTokenParser::parse_list() {
    return parse_list_until(TokenKind::End);
}

bool TypeTokenParser::at_end() const {
    return cursor_ >= tokens_.size() || !type_token(tokens_[cursor_]);
}

const Token& TypeTokenParser::current() const {
    return tokens_[std::min(cursor_, tokens_.size() - 1)];
}

bool TypeTokenParser::at(TokenKind kind) const {
    return !at_end() && current().kind == kind;
}

bool TypeTokenParser::at_operator(std::string_view op) const {
    return !at_end() && current().kind == TokenKind::Operator && current().text == op;
}

bool TypeTokenParser::at_identifier(std::string_view text) const {
    return !at_end() && current().kind == TokenKind::Identifier && current().text == text;
}

bool TypeTokenParser::at_ellipsis() const {
    return cursor_ + 2 < tokens_.size() && tokens_[cursor_].kind == TokenKind::Dot &&
           tokens_[cursor_ + 1].kind == TokenKind::Dot &&
           tokens_[cursor_ + 2].kind == TokenKind::Dot;
}

bool TypeTokenParser::stop_at(std::initializer_list<TokenKind> stops) const {
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

bool TypeTokenParser::match(TokenKind kind) {
    if (!at(kind)) {
        return false;
    }
    ++cursor_;
    return true;
}

bool TypeTokenParser::match_operator(std::string_view op) {
    if (!at_operator(op)) {
        return false;
    }
    ++cursor_;
    return true;
}

bool TypeTokenParser::match_identifier(std::string_view text) {
    if (!at_identifier(text)) {
        return false;
    }
    ++cursor_;
    return true;
}

bool TypeTokenParser::match_scope_separator() {
    if (at_ellipsis()) {
        return false;
    }
    if (match(TokenKind::Dot)) {
        return true;
    }
    if (cursor_ + 1 < tokens_.size() && tokens_[cursor_].kind == TokenKind::Colon &&
        tokens_[cursor_ + 1].kind == TokenKind::Colon) {
        cursor_ += 2;
        return true;
    }
    return false;
}

bool TypeTokenParser::match_ellipsis() {
    if (!at_ellipsis()) {
        return false;
    }
    cursor_ += 3;
    return true;
}

SourceRange TypeTokenParser::range_between(size_t begin, size_t end) const {
    SourceRange range;
    if (begin >= tokens_.size() || begin >= end) {
        return range;
    }
    range.start = tokens_[begin].location;
    range.end = token_end_location(tokens_[end - 1]);
    return range;
}

std::string TypeTokenParser::text_between(size_t begin, size_t end) const {
    if (begin >= tokens_.size() || begin >= end) {
        return {};
    }
    SourceLocation cursor = tokens_[begin].location;
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        const Token& token = tokens_[i];
        if (!type_token(token)) {
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

TypeRef TypeTokenParser::make_node(TypeKind kind, size_t begin, size_t end) const {
    TypeRef type;
    type.kind = kind;
    type.malformed = kind == TypeKind::Unknown;
    type.location = begin < tokens_.size() ? tokens_[begin].location : SourceLocation{};
    type.range = range_between(begin, end);
    return type;
}

TypeRef TypeTokenParser::parse_type(std::initializer_list<TokenKind> stops) {
    const size_t begin = cursor_;
    TypeRef type = parse_prefix(stops);
    if (!stop_at(stops) && match_ellipsis()) {
        TypeRef out = make_node(TypeKind::PackExpansion, begin, cursor_);
        out.children.push_back(std::move(type));
        out.range = range_between(begin, cursor_);
        return out;
    }
    if (!stop_at(stops) && match(TokenKind::Arrow)) {
        std::vector<TypeRef> params;
        if (type.kind == TypeKind::Unknown && !has_type_ref(type)) {
            params = {};
        } else if (type.kind == TypeKind::Function && !type.children.empty() &&
                   type_ref_is_void(type.children.front())) {
            params.assign(type.children.begin() + 1, type.children.end());
        } else {
            params.push_back(std::move(type));
        }
        TypeRef out = parse_function_type(begin, std::move(params));
        out.range = range_between(begin, cursor_);
        return out;
    }
    return type;
}

TypeRef TypeTokenParser::parse_prefix(std::initializer_list<TokenKind> stops) {
    const size_t begin = cursor_;
    if (match_operator("*")) {
        TypeRef type = make_node(TypeKind::Pointer, begin, cursor_);
        type.children.push_back(parse_type(stops));
        type.range = range_between(begin, cursor_);
        return type;
    }
    if (match_operator("&")) {
        TypeRef type = make_node(TypeKind::Reference, begin, cursor_);
        type.children.push_back(parse_type(stops));
        type.range = range_between(begin, cursor_);
        return type;
    }
    return parse_primary(stops);
}

TypeRef TypeTokenParser::parse_primary(std::initializer_list<TokenKind> stops) {
    const size_t begin = cursor_;
    if (match_identifier("fn") && at(TokenKind::LParen)) {
        return parse_fn_type(begin);
    }
    if (match(TokenKind::LParen)) {
        return parse_paren_or_function(begin, stops);
    }
    if (match_operator("-") || match_operator("+")) {
        if (at(TokenKind::Number)) {
            ++cursor_;
            TypeRef type = make_node(TypeKind::Value, begin, cursor_);
            type.value = text_between(begin, cursor_);
            return type;
        }
        return make_node(TypeKind::Unknown, begin, cursor_);
    }
    if (match(TokenKind::Number)) {
        TypeRef type = make_node(TypeKind::Value, begin, cursor_);
        type.value = text_between(begin, cursor_);
        return type;
    }
    if ((at_identifier("struct") || at_identifier("union") || at_identifier("enum")) &&
        cursor_ + 1 < tokens_.size() && tokens_[cursor_ + 1].kind == TokenKind::Identifier) {
        return parse_c_tag_name(begin);
    }
    if (at(TokenKind::Identifier)) {
        return parse_name_or_template(begin);
    }
    if (!stop_at(stops)) {
        ++cursor_;
    }
    return make_node(TypeKind::Unknown, begin, cursor_);
}

TypeRef TypeTokenParser::parse_function_type(size_t begin, std::vector<TypeRef> params) {
    TypeRef type = make_node(TypeKind::Function, begin, cursor_);
    type.children.push_back(parse_type({}));
    type.children.insert(type.children.end(), std::make_move_iterator(params.begin()),
                         std::make_move_iterator(params.end()));
    type.range = range_between(begin, cursor_);
    return type;
}

TypeRef TypeTokenParser::parse_fn_type(size_t begin) {
    match(TokenKind::LParen);
    std::vector<TypeRef> params = parse_list_until(TokenKind::RParen);
    if (!match(TokenKind::RParen)) {
        return make_node(TypeKind::Unknown, begin, cursor_);
    }
    if (match(TokenKind::Arrow)) {
        return parse_function_type(begin, std::move(params));
    }
    TypeRef type = make_node(TypeKind::Function, begin, cursor_);
    type.children.push_back(void_type_ref(type.location));
    type.children.insert(type.children.end(), std::make_move_iterator(params.begin()),
                         std::make_move_iterator(params.end()));
    type.range = range_between(begin, cursor_);
    return type;
}

TypeRef TypeTokenParser::parse_paren_or_function(size_t begin,
                                                 std::initializer_list<TokenKind> stops) {
    std::vector<TypeRef> params = parse_list_until(TokenKind::RParen);
    if (!match(TokenKind::RParen)) {
        return make_node(TypeKind::Unknown, begin, cursor_);
    }
    if (match(TokenKind::Arrow)) {
        return parse_function_type(begin, std::move(params));
    }
    if (params.size() == 1) {
        TypeRef type = std::move(params.front());
        type.range = range_between(begin, cursor_);
        return type;
    }
    if (!stop_at(stops)) {
        return make_node(TypeKind::Unknown, begin, cursor_);
    }
    return make_node(TypeKind::Unknown, begin, cursor_);
}

TypeRef TypeTokenParser::parse_c_tag_name(size_t begin) {
    ++cursor_;
    ++cursor_;
    while (match_scope_separator()) {
        if (!match(TokenKind::Identifier)) {
            return make_node(TypeKind::Unknown, begin, cursor_);
        }
    }
    const std::string text = text_between(begin, cursor_);
    TypeRef type =
        make_node(text.find('.') == std::string::npos && text.find("::") == std::string::npos
                      ? TypeKind::Named
                      : TypeKind::Qualified,
                  begin, cursor_);
    type.name = text;
    return type;
}

TypeRef TypeTokenParser::parse_name_or_template(size_t begin) {
    ++cursor_;
    while (match_scope_separator()) {
        if (!match(TokenKind::Identifier)) {
            return make_node(TypeKind::Unknown, begin, cursor_);
        }
    }
    const std::string text = text_between(begin, cursor_);
    TypeRef type =
        make_node(text.find('.') == std::string::npos && text.find("::") == std::string::npos
                      ? TypeKind::Named
                      : TypeKind::Qualified,
                  begin, cursor_);
    type.name = text;
    while (match(TokenKind::LBracket)) {
        const size_t inner_begin = cursor_;
        std::vector<TypeRef> args = parse_list_until(TokenKind::RBracket);
        const size_t inner_end = cursor_;
        if (!match(TokenKind::RBracket)) {
            return make_node(TypeKind::Unknown, begin, cursor_);
        }
        const TypeKind wrapper = wrapper_type_kind(type.name);
        if (wrapper != TypeKind::Unknown && type.kind != TypeKind::Template) {
            TypeRef wrapped = make_node(wrapper, begin, cursor_);
            if (!args.empty()) {
                wrapped.children.push_back(std::move(args.front()));
            }
            type = std::move(wrapped);
            continue;
        }
        if (type.kind == TypeKind::Template && type.name == "array") {
            TypeRef fixed = make_node(TypeKind::FixedArray, begin, cursor_);
            fixed.children.push_back(std::move(type));
            args = shape_dim_refs(std::move(args));
            fixed.children.insert(fixed.children.end(), std::make_move_iterator(args.begin()),
                                  std::make_move_iterator(args.end()));
            fixed.value = trim_string(text_between(inner_begin, inner_end));
            type = std::move(fixed);
            continue;
        }
        if (type_metadata_base_args_are_types(type)) {
            TypeRef shaped = make_node(TypeKind::Shaped, begin, cursor_);
            shaped.children.push_back(std::move(type));
            args = shape_dim_refs(std::move(args));
            shaped.children.insert(shaped.children.end(), std::make_move_iterator(args.begin()),
                                   std::make_move_iterator(args.end()));
            shaped.value = trim_string(text_between(inner_begin, inner_end));
            type = std::move(shaped);
            continue;
        }
        if (type.kind != TypeKind::Named && type.kind != TypeKind::Qualified) {
            return make_node(TypeKind::Unknown, begin, cursor_);
        }
        TypeRef templ = make_node(TypeKind::Template, begin, cursor_);
        templ.name = type.name;
        templ.children = std::move(args);
        type = std::move(templ);
    }
    while (match_operator("<")) {
        TypeRef templ = make_node(TypeKind::Template, begin, cursor_);
        templ.name = type.name;
        templ.children = parse_angle_template_args();
        if (!match_operator(">")) {
            return make_node(TypeKind::Unknown, begin, cursor_);
        }
        templ.range = range_between(begin, cursor_);
        type = std::move(templ);
    }
    return type;
}

std::vector<TypeRef> TypeTokenParser::parse_angle_template_args() {
    std::vector<TypeRef> out;
    while (!at_end() && !at_operator(">")) {
        if (at(TokenKind::Comma)) {
            ++cursor_;
            continue;
        }
        const size_t begin = cursor_;
        int paren_depth = 0;
        int bracket_depth = 0;
        int angle_depth = 0;
        while (!at_end()) {
            if (paren_depth == 0 && bracket_depth == 0 && angle_depth == 0 &&
                (at(TokenKind::Comma) || at_operator(">"))) {
                break;
            }
            if (at(TokenKind::LParen)) {
                ++paren_depth;
            } else if (at(TokenKind::RParen) && paren_depth > 0) {
                --paren_depth;
            } else if (at(TokenKind::LBracket)) {
                ++bracket_depth;
            } else if (at(TokenKind::RBracket) && bracket_depth > 0) {
                --bracket_depth;
            } else if (at_operator("<")) {
                ++angle_depth;
            } else if (at_operator(">") && angle_depth > 0) {
                --angle_depth;
            }
            ++cursor_;
        }
        TypeTokenParser parser(tokens_.subspan(begin, cursor_ - begin));
        out.push_back(parser.parse());
        if (at(TokenKind::Comma)) {
            ++cursor_;
        }
    }
    return out;
}

std::vector<TypeRef> TypeTokenParser::parse_list_until(TokenKind close) {
    std::vector<TypeRef> out;
    while (!at_end() && !at(close)) {
        if (at(TokenKind::Comma)) {
            ++cursor_;
            continue;
        }
        out.push_back(parse_type({close, TokenKind::Comma}));
        if (at(TokenKind::Comma)) {
            ++cursor_;
        }
    }
    return out;
}

} // namespace dudu
