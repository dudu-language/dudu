#include "dudu/lexer.hpp"
#include "dudu/parser_internal.hpp"
#include "dudu/parser_utils.hpp"

#include <sstream>

namespace dudu {
namespace {

SourceRange range_for_line_text(SourceLocation location, std::string_view text) {
    return {.start = location,
            .end = {.file = location.file,
                    .line = location.line,
                    .column = location.column + static_cast<int>(text.size())}};
}

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

void attach_out_of_line_method(ModuleAst& module, FunctionDecl method) {
    for (ClassDecl& klass : module.classes) {
        if (klass.name == method.receiver_type) {
            klass.methods.push_back(std::move(method));
            return;
        }
    }
    throw CompileError(method.location,
                       "out-of-line method receiver is not a known class: " + method.receiver_type);
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
            if (fn.receiver_type.empty()) {
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
    throw CompileError(current().location, message);
}

void Parser::skip_newlines() {
    while (match(TokenKind::Newline)) {
    }
}

void Parser::require_no_decorators(const std::vector<Decorator>& decorators,
                                   std::string_view target) const {
    if (!decorators.empty()) {
        throw CompileError(decorators.front().location,
                           "decorators are not valid before " + std::string(target));
    }
}

Visibility Parser::parse_visibility() {
    if (match_identifier("public")) {
        throw CompileError(previous().location,
                           "explicit visibility keywords are not supported; use normal public "
                           "names or leading underscore private names");
    }
    if (match_identifier("private")) {
        throw CompileError(previous().location,
                           "explicit visibility keywords are not supported; use normal public "
                           "names or leading underscore private names");
    }
    return Visibility::Default;
}

Decorator Parser::parse_decorator(const Token& at_token) {
    std::string text = join_until({TokenKind::Newline});
    consume(TokenKind::Newline, "expected newline after decorator");
    Decorator decorator;
    decorator.location = at_token.location;
    decorator.expr = parse_expr_text(text, at_token.location);
    decorator.text = std::move(text);
    return decorator;
}

ImportDecl Parser::parse_import(const Token& start) {
    if (match_identifier("c")) {
        return parse_foreign_import(start, ImportKind::ForeignC);
    }
    if (match_identifier("cpp")) {
        return parse_foreign_import(start, ImportKind::ForeignCpp);
    }

    ImportDecl import;
    import.kind = ImportKind::Module;
    import.location = start.location;
    import.module_path = parse_path();
    if (match_identifier("as")) {
        import.alias = consume_identifier("expected alias after as").text;
    }
    consume(TokenKind::Newline, "expected newline after import");
    return import;
}

ImportDecl Parser::parse_foreign_import(const Token& start, ImportKind kind) {
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
    consume(TokenKind::Newline, "expected newline after foreign import");
    return import;
}

ImportDecl Parser::parse_from_import(const Token& start) {
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

std::vector<Stmt> Parser::parse_statement_block() {
    std::vector<Stmt> out;
    if (!match(TokenKind::Indent)) {
        return out;
    }
    while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
        if (match(TokenKind::Newline)) {
            continue;
        }
        const SourceLocation location = current().location;
        JoinedTokens joined = join_until_with_range({TokenKind::Newline});
        std::string text = std::move(joined.text);
        const SourceRange range =
            joined.has_tokens ? joined.range : range_for_line_text(location, text);
        consume(TokenKind::Newline, "expected newline after statement");
        std::vector<Stmt> children;
        if (at(TokenKind::Indent)) {
            children = parse_statement_block();
        }
        out.push_back(statement_from_text(std::move(text), std::move(joined.source_text), location,
                                          range, std::move(children)));
    }
    consume(TokenKind::Dedent, "expected dedent after block");
    return out;
}

std::string Parser::join_until(std::initializer_list<TokenKind> stops) {
    return join_until_with_range(stops).text;
}

Parser::JoinedTokens Parser::join_until_with_range(std::initializer_list<TokenKind> stops) {
    JoinedTokens joined;
    std::ostringstream out;
    std::ostringstream source_out;
    SourceLocation source_cursor;
    bool first = true;
    TokenKind previous_kind = TokenKind::End;
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
            source_cursor = token.location;
            joined.has_tokens = true;
        }
        joined.range.end = token_end_location(token);
        append_source_token(source_out, source_cursor, token);
        if (!first && parser_needs_space_between(previous_kind, current().kind)) {
            out << ' ';
        }
        out << current().text;
        first = false;
        previous_kind = current().kind;
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
    joined.text = out.str();
    joined.source_text = source_out.str();
    return joined;
}

ModuleAst parse_module(std::span<const Token> tokens) {
    return Parser(tokens).parse();
}

ModuleAst parse_source(std::string_view source, const std::filesystem::path& file) {
    return parse_module(lex_source(source, file));
}

} // namespace dudu
