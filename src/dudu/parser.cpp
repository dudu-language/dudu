#include "dudu/ast_expr.hpp"
#include "dudu/ast_expr_token_parser.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/ast_type_token_parser.hpp"
#include "dudu/lexer.hpp"
#include "dudu/parser_internal.hpp"
#include "dudu/parser_utils.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

SourceLocation token_end_location(const Token& token) {
    SourceLocation end = token.location;
    for (const char c : token.text) {
        if (c == '\n') {
            ++end.line;
            end.column = 1;
        } else {
            ++end.column;
        }
    }
    return end;
}

void append_source_token(std::ostringstream& out, SourceLocation& cursor, const Token& token) {
    while (cursor.line < token.location.line) {
        out << '\n';
        ++cursor.line;
        cursor.column = 1;
    }
    while (cursor.column < token.location.column) {
        out << ' ';
        ++cursor.column;
    }
    out << token.text;
    cursor = token_end_location(token);
}

const TypeRef* malformed_type_node(const TypeRef& type) {
    if (type.kind == TypeKind::Unknown && type.malformed) {
        return &type;
    }
    for (const TypeRef& child : type.children) {
        if (const TypeRef* malformed = malformed_type_node(child)) {
            return malformed;
        }
    }
    return nullptr;
}

void attach_out_of_line_method(ModuleAst& module, FunctionDecl method) {
    const std::string receiver_type = function_receiver_type_text(method);
    for (ClassDecl& klass : module.classes) {
        if (klass.name == receiver_type) {
            klass.methods.push_back(std::move(method));
            return;
        }
    }
    throw CompileError(method.location,
                       "out-of-line method receiver is not a known class: " + receiver_type);
}

std::vector<Token> syntax_piece_tokens(std::span<const Token> tokens) {
    std::vector<Token> out;
    out.reserve(tokens.size());
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Newline || token.kind == TokenKind::Indent ||
            token.kind == TokenKind::Dedent) {
            continue;
        }
        out.push_back(token);
    }
    SourceLocation end_location;
    if (!out.empty()) {
        end_location = token_end_location(out.back());
    }
    out.push_back({.kind = TokenKind::End, .text = "", .location = end_location});
    return out;
}

} // namespace

Parser::Parser(std::span<const Token> tokens) : tokens_(tokens) {
}

ModuleAst Parser::parse() {
    ModuleAst module;
    std::vector<Decorator> decorators;

    while (!at(TokenKind::End)) {
        skip_newlines();
        if (at(TokenKind::End)) {
            break;
        }
        if (match(TokenKind::At)) {
            decorators.push_back(parse_decorator(previous()));
            continue;
        }

        Visibility visibility = parse_visibility();
        if (match_identifier("import")) {
            require_no_decorators(decorators, "import");
            module.imports.push_back(parse_import(previous()));
        } else if (match_identifier("from")) {
            require_no_decorators(decorators, "from import");
            module.imports.push_back(parse_from_import(previous()));
        } else if (match_identifier("class")) {
            module.classes.push_back(parse_class(previous(), visibility, decorators));
            decorators.clear();
        } else if (match_identifier("enum")) {
            require_no_decorators(decorators, "enum");
            module.enums.push_back(parse_enum(previous()));
        } else if (match_identifier("type")) {
            require_no_decorators(decorators, "type declaration");
            parse_type_decl(previous(), module);
        } else if (match_identifier("def")) {
            FunctionDecl fn = parse_function(previous(), visibility, decorators);
            if (!function_has_receiver_type(fn)) {
                module.functions.push_back(std::move(fn));
            } else {
                attach_out_of_line_method(module, std::move(fn));
            }
            decorators.clear();
        } else if (is_all_caps_identifier(current())) {
            require_no_decorators(decorators, "constant");
            module.constants.push_back(parse_constant());
        } else if (check_text("static_assert")) {
            require_no_decorators(decorators, "static_assert");
            module.static_asserts.push_back(parse_static_assert());
        } else {
            fail_current("expected top-level import, class, def, or constant");
        }
    }

    require_no_decorators(decorators, "end of file");
    validate_import_bindings(module.imports);
    return module;
}

const Token& Parser::current() const {
    return tokens_[cursor_];
}

const Token& Parser::previous() const {
    return tokens_[cursor_ - 1];
}

bool Parser::at(TokenKind kind) const {
    return current().kind == kind;
}

bool Parser::check_text(std::string_view text) const {
    return current().kind == TokenKind::Identifier && current().text == text;
}

bool Parser::match(TokenKind kind) {
    if (!at(kind)) {
        return false;
    }
    ++cursor_;
    return true;
}

bool Parser::match_identifier(std::string_view text) {
    if (!check_text(text)) {
        return false;
    }
    ++cursor_;
    return true;
}

const Token& Parser::consume(TokenKind kind, std::string_view message) {
    if (!at(kind)) {
        fail_current(std::string(message));
    }
    return tokens_[cursor_++];
}

const Token& Parser::consume_identifier(std::string_view message) {
    return consume(TokenKind::Identifier, message);
}

void Parser::fail_current(const std::string& message) const {
    throw CompileError(current().location, message, "dudu.parser.syntax");
}

void Parser::skip_newlines() {
    while (match(TokenKind::Newline)) {
    }
}

void Parser::require_no_decorators(const std::vector<Decorator>& decorators,
                                   std::string_view target) const {
    if (!decorators.empty()) {
        throw CompileError(decorators.front().location,
                           "decorators are not valid before " + std::string(target),
                           "dudu.parser.decorator");
    }
}

Visibility Parser::parse_visibility() {
    if (match_identifier("public")) {
        throw CompileError(previous().location,
                           "explicit visibility keywords are not supported; use normal public "
                           "names or leading underscore private names",
                           "dudu.parser.visibility");
    }
    if (match_identifier("private")) {
        throw CompileError(previous().location,
                           "explicit visibility keywords are not supported; use normal public "
                           "names or leading underscore private names",
                           "dudu.parser.visibility");
    }
    return Visibility::Default;
}

Decorator Parser::parse_decorator(const Token& at_token) {
    JoinedTokens expression = join_until_with_range({TokenKind::Newline});
    consume(TokenKind::Newline, "expected newline after decorator");
    Decorator decorator;
    decorator.location = at_token.location;
    decorator.expr = parse_expr_piece(expression);
    return decorator;
}

ImportDecl Parser::parse_import(const Token& start) {
    const size_t statement_begin = cursor_ - 1;
    if (match_identifier("c")) {
        return parse_foreign_import(start, ImportKind::ForeignC, statement_begin);
    }
    if (match_identifier("cpp")) {
        return parse_foreign_import(start, ImportKind::ForeignCpp, statement_begin);
    }

    ImportDecl import;
    import.kind = ImportKind::Module;
    import.location = start.location;
    import.module_path = parse_path();
    if (match_identifier("as")) {
        import.alias = consume_identifier("expected alias after as").text;
    }
    const JoinedTokens source = join_tokens(statement_begin, cursor_);
    import.range = source.range;
    consume(TokenKind::Newline, "expected newline after import");
    return import;
}

ImportDecl Parser::parse_foreign_import(const Token& start, ImportKind kind,
                                        size_t statement_begin) {
    ImportDecl import;
    import.kind = kind;
    import.location = start.location;
    const Token& header = consume(TokenKind::String, "expected quoted foreign header");
    import.module_path = header.text;
    import.module_range.start = header.location;
    import.module_range.end = header.location;
    import.module_range.end.column += static_cast<int>(header.text.size());
    if (header.text.size() >= 2) {
        ++import.module_range.start.column;
        --import.module_range.end.column;
    }
    if (match_identifier("as")) {
        import.alias = consume_identifier("expected alias after as").text;
    }
    const JoinedTokens source = join_tokens(statement_begin, cursor_);
    import.range = source.range;
    consume(TokenKind::Newline, "expected newline after foreign import");
    return import;
}

ImportDecl Parser::parse_from_import(const Token& start) {
    const size_t statement_begin = cursor_ - 1;
    ImportDecl import;
    import.kind = ImportKind::From;
    import.location = start.location;
    import.module_path = parse_path();
    if (!match_identifier("import")) {
        fail_current("expected import after module path");
    }
    import.imported_name = consume_identifier("expected imported name").text;
    if (match_identifier("as")) {
        import.alias = consume_identifier("expected alias after as").text;
    }
    const JoinedTokens source = join_tokens(statement_begin, cursor_);
    import.range = source.range;
    consume(TokenKind::Newline, "expected newline after from import");
    return import;
}

std::string Parser::parse_path() {
    std::string out = consume_identifier("expected module path").text;
    while (match(TokenKind::Dot)) {
        out += '.';
        out += consume_identifier("expected name after dot").text;
    }
    return out;
}

Parser::JoinedTokens Parser::join_until_with_range(std::initializer_list<TokenKind> stops) {
    JoinedTokens joined;
    joined.begin = cursor_;
    int bracket_depth = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    while (!at(TokenKind::End)) {
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (!inside_group) {
            bool stop = false;
            for (TokenKind kind : stops) {
                stop = stop || at(kind);
            }
            if (stop) {
                break;
            }
        }
        if (inside_group &&
            (at(TokenKind::Newline) || at(TokenKind::Indent) || at(TokenKind::Dedent))) {
            ++cursor_;
            continue;
        }
        const Token& token = current();
        if (!joined.has_tokens) {
            joined.range.start = token.location;
            joined.has_tokens = true;
        }
        joined.range.end = token_end_location(token);
        if (current().kind == TokenKind::LBracket) {
            ++bracket_depth;
        } else if (current().kind == TokenKind::RBracket) {
            --bracket_depth;
        } else if (current().kind == TokenKind::LParen) {
            ++paren_depth;
        } else if (current().kind == TokenKind::RParen) {
            --paren_depth;
        } else if (current().kind == TokenKind::LBrace) {
            ++brace_depth;
        } else if (current().kind == TokenKind::RBrace) {
            --brace_depth;
        }
        ++cursor_;
    }
    joined.end = cursor_;
    return joined;
}

Parser::JoinedTokens Parser::join_tokens(size_t begin, size_t end) const {
    JoinedTokens joined;
    joined.begin = begin;
    joined.end = std::min(end, tokens_.size());
    for (size_t index = begin; index < end && index < tokens_.size(); ++index) {
        const Token& token = tokens_[index];
        if (!joined.has_tokens) {
            joined.range.start = token.location;
            joined.has_tokens = true;
        }
        joined.range.end = token_end_location(token);
    }
    return joined;
}

std::string Parser::source_text_for_tokens(size_t begin, size_t end) const {
    std::ostringstream out;
    SourceLocation source_cursor;
    bool has_tokens = false;
    for (size_t index = begin; index < end && index < tokens_.size(); ++index) {
        const Token& token = tokens_[index];
        if (!has_tokens) {
            source_cursor = token.location;
            has_tokens = true;
        }
        append_source_token(out, source_cursor, token);
    }
    return out.str();
}

Expr Parser::parse_expr_piece(const JoinedTokens& piece) const {
    if (!piece.has_tokens) {
        return make_expr(ExprKind::Missing, "", piece.range.start);
    }
    std::vector<Token> tokens =
        syntax_piece_tokens(tokens_.subspan(piece.begin, piece.end - piece.begin));
    ExprTokenParser parser(tokens);
    Expr expr = parser.parse();
    if (expr.kind == ExprKind::Unknown) {
        const std::string spelling = trim_string(source_text_for_tokens(piece.begin, piece.end));
        throw CompileError(expr.location,
                           "unsupported expression: " +
                               (spelling.empty() ? display_expr(expr) : spelling),
                           "dudu.parser.unsupported_expression");
    }
    return expr;
}

TypeRef Parser::parse_type_piece(const JoinedTokens& piece) const {
    if (!piece.has_tokens) {
        return make_type(TypeKind::Unknown, "", piece.range.start);
    }
    std::vector<Token> tokens =
        syntax_piece_tokens(tokens_.subspan(piece.begin, piece.end - piece.begin));
    TypeTokenParser parser(tokens);
    TypeRef type = parser.parse();
    if (const TypeRef* malformed = malformed_type_node(type)) {
        std::string spelling = trim_string(source_text_for_tokens(piece.begin, piece.end));
        const std::string message =
            spelling.empty() ? "malformed type syntax" : "malformed type syntax: " + spelling;
        throw CompileError(malformed->location.line > 0 ? malformed->location : type.location,
                           message, "dudu.parser.malformed_type");
    }
    return type;
}

ModuleAst parse_module(std::span<const Token> tokens) {
    return Parser(tokens).parse();
}

ModuleAst parse_source(std::string_view source, const std::filesystem::path& file) {
    return parse_module(lex_source(source, file));
}

} // namespace dudu
