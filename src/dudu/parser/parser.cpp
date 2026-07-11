#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/parser/ast_expr_token_parser.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/parser/ast_type_token_parser.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/parser/parser_doc_comments.hpp"
#include "dudu/parser/parser_internal.hpp"
#include "dudu/parser/parser_utils.hpp"

#include <algorithm>
#include <iterator>
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
    const std::string receiver_type = type_ref_head_name(method.receiver_type_ref);
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
    out.push_back({.text = "", .location = end_location, .kind = TokenKind::End});
    return out;
}

} // namespace

Parser::Parser(std::span<const Token> tokens, std::vector<ParseDiagnostic>* diagnostics)
    : tokens_(tokens), diagnostics_(diagnostics) {
}

ModuleAst Parser::parse() {
    ModuleAst module;
    std::vector<Decorator> decorators;
    bool can_accept_docstring = true;

    while (!at(TokenKind::End)) {
        skip_newlines();
        if (at(TokenKind::End)) {
            break;
        }
        if (can_accept_docstring && at(TokenKind::String)) {
            require_no_decorators(decorators, "module docstring");
            const JoinedTokens doc = join_tokens(cursor_, cursor_ + 1);
            const Expr expr = parse_expr_piece(doc);
            module.doc_comment = normalize_docstring_text(expr.value);
            ++cursor_;
            consume(TokenKind::Newline, "expected newline after module docstring");
            can_accept_docstring = false;
            continue;
        }
        can_accept_docstring = false;
        if (at(TokenKind::String)) {
            throw CompileError(current().location,
                               "misplaced docstring; module docstrings must be the first "
                               "statement in the file",
                               "dudu.parser.misplaced_docstring");
        }
        if (match(TokenKind::At)) {
            decorators.push_back(parse_decorator(previous()));
            continue;
        }

        const size_t item_begin = cursor_;
        try {
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
            } else if (current().kind == TokenKind::Identifier && at_next(TokenKind::Colon)) {
                require_no_decorators(decorators, "constant");
                module.constants.push_back(parse_constant());
            } else if (check_text("static_assert")) {
                require_no_decorators(decorators, "static_assert");
                module.static_asserts.push_back(parse_static_assert());
            } else {
                fail_current("expected top-level import, class, def, or constant");
            }
        } catch (const CompileError& error) {
            if (!recovering()) {
                throw;
            }
            record_diagnostic(error);
            decorators.clear();
            synchronize_top_level(item_begin);
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

bool Parser::at_next(TokenKind kind) const {
    return cursor_ + 1 < tokens_.size() && tokens_[cursor_ + 1].kind == kind;
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

bool Parser::recovering() const {
    return diagnostics_ != nullptr;
}

void Parser::record_diagnostic(const CompileError& error) {
    diagnostics_->push_back({.location = error.location(),
                             .message = error.what(),
                             .code = error.code(),
                             .data_name = error.data_name()});
}

int Parser::layout_depth_before(size_t cursor) const {
    int depth = 0;
    for (size_t index = 0; index < cursor && index < tokens_.size(); ++index) {
        if (tokens_[index].kind == TokenKind::Indent) {
            ++depth;
        } else if (tokens_[index].kind == TokenKind::Dedent) {
            depth = std::max(0, depth - 1);
        }
    }
    return depth;
}

void Parser::synchronize_block_item(size_t failed_cursor) {
    const int target_depth = layout_depth_before(failed_cursor);
    if (cursor_ <= failed_cursor && !at(TokenKind::End)) {
        ++cursor_;
    }
    int depth = layout_depth_before(cursor_);
    bool crossed_line = false;
    while (!at(TokenKind::End)) {
        if (at(TokenKind::Dedent)) {
            if (depth <= target_depth) {
                return;
            }
            --depth;
            ++cursor_;
            crossed_line = true;
            continue;
        }
        if (at(TokenKind::Indent)) {
            ++depth;
            ++cursor_;
            continue;
        }
        if (at(TokenKind::Newline)) {
            ++cursor_;
            crossed_line = true;
            continue;
        }
        if (crossed_line && depth == target_depth) {
            return;
        }
        ++cursor_;
    }
}

void Parser::synchronize_top_level(size_t failed_cursor) {
    synchronize_block_item(failed_cursor);
    while (at(TokenKind::Dedent) || at(TokenKind::Newline)) {
        ++cursor_;
    }
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
    if (match_identifier("cxx")) {
        return parse_foreign_import(start, ImportKind::ForeignCxx, statement_begin);
    }
    if (match_identifier("cpp")) {
        return parse_foreign_import(start, ImportKind::ForeignCpp, statement_begin);
    }

    ImportDecl import;
    import.kind = ImportKind::Module;
    import.location = start.location;
    const size_t module_begin = cursor_;
    import.module_path = parse_path();
    import.module_range = join_tokens(module_begin, cursor_).range;
    if (match_identifier("as")) {
        const Token& alias = consume_identifier("expected alias after as");
        import.alias = alias.text;
        import.alias_range.start = alias.location;
        import.alias_range.end = alias.location;
        import.alias_range.end.column += static_cast<int>(alias.text.size());
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
    import.native_include_style = NativeIncludeStyle::Path;
    import.location = start.location;
    const Token& header = consume(TokenKind::String, "expected quoted foreign header");
    import.module_path = header.text;
    import.module_range.start = header.location;
    import.module_range.end = header.location;
    import.module_range.end.column += static_cast<int>(header.text.size());
    if (header.text.size() >= 2) {
        ++import.module_range.start.column;
        --import.module_range.end.column;
        import.module_path = import.module_path.substr(1, import.module_path.size() - 2);
    }
    if (match_identifier("as")) {
        const Token& alias = consume_identifier("expected alias after as");
        import.alias = alias.text;
        import.alias_range.start = alias.location;
        import.alias_range.end = alias.location;
        import.alias_range.end.column += static_cast<int>(alias.text.size());
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
    const size_t module_begin = cursor_;
    import.module_path = parse_path();
    import.module_range = join_tokens(module_begin, cursor_).range;
    if (!match_identifier("import")) {
        fail_current("expected import after module path");
    }
    if (import.module_path == "c" || import.module_path == "c.path" ||
        import.module_path == "cxx" || import.module_path == "cxx.path" ||
        import.module_path == "cpp" || import.module_path == "cpp.path") {
        const bool path_mode = import.module_path.ends_with(".path");
        ImportKind kind = ImportKind::ForeignCpp;
        if (import.module_path == "c" || import.module_path == "c.path") {
            kind = ImportKind::ForeignC;
        } else if (import.module_path == "cxx" || import.module_path == "cxx.path") {
            kind = ImportKind::ForeignCxx;
        }
        const size_t native_mode_index = path_mode ? module_begin + 2 : module_begin;
        return parse_foreign_from_import(
            start, kind, path_mode ? NativeIncludeStyle::Path : NativeIncludeStyle::System,
            statement_begin, module_begin, native_mode_index);
    }
    const Token& imported_name = consume_identifier("expected imported name");
    import.imported_name = imported_name.text;
    import.imported_name_range.start = imported_name.location;
    import.imported_name_range.end = imported_name.location;
    import.imported_name_range.end.column += static_cast<int>(imported_name.text.size());
    if (match_identifier("as")) {
        const Token& alias = consume_identifier("expected alias after as");
        import.alias = alias.text;
        import.alias_range.start = alias.location;
        import.alias_range.end = alias.location;
        import.alias_range.end.column += static_cast<int>(alias.text.size());
    }
    const JoinedTokens source = join_tokens(statement_begin, cursor_);
    import.range = source.range;
    consume(TokenKind::Newline, "expected newline after from import");
    return import;
}

ImportDecl Parser::parse_foreign_from_import(const Token& start, ImportKind kind,
                                             NativeIncludeStyle include_style,
                                             size_t statement_begin, size_t native_module_begin,
                                             size_t native_mode_index) {
    ImportDecl import;
    import.kind = kind;
    import.native_include_style = include_style;
    import.location = start.location;
    import.native_language_range = join_tokens(native_module_begin, native_module_begin + 1).range;
    if (include_style == NativeIncludeStyle::Path) {
        import.native_path_mode_range = join_tokens(native_mode_index, native_mode_index + 1).range;
    }
    const JoinedTokens header = parse_foreign_header_target();
    import.module_path = trim_string(token_source_spelling(header.begin, header.end));
    import.module_range = header.range;
    if (import.module_path.size() >= 2 && import.module_path.front() == '"' &&
        import.module_path.back() == '"') {
        import.module_path = import.module_path.substr(1, import.module_path.size() - 2);
        ++import.module_range.start.column;
        --import.module_range.end.column;
        import.native_include_style = NativeIncludeStyle::Path;
    }
    if (import.module_path.empty()) {
        throw CompileError(current().location, "expected native header after import");
    }
    if (match_identifier("as")) {
        const Token& alias = consume_identifier("expected alias after as");
        import.alias = alias.text;
        import.alias_range.start = alias.location;
        import.alias_range.end = alias.location;
        import.alias_range.end.column += static_cast<int>(alias.text.size());
    }
    const JoinedTokens source = join_tokens(statement_begin, cursor_);
    import.range = source.range;
    consume(TokenKind::Newline, "expected newline after native import");
    return import;
}

Parser::JoinedTokens Parser::parse_foreign_header_target() {
    const size_t begin = cursor_;
    while (!at(TokenKind::End) && !at(TokenKind::Newline)) {
        if (current().kind == TokenKind::Identifier && current().text == "as") {
            break;
        }
        ++cursor_;
    }
    if (begin == cursor_) {
        fail_current("expected native header after import");
    }
    return join_tokens(begin, cursor_);
}

std::string Parser::parse_path() {
    std::string out{consume_identifier("expected module path").text};
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
    int layout_depth = 0;
    while (!at(TokenKind::End)) {
        const bool inside_group = bracket_depth != 0 || paren_depth != 0 || brace_depth != 0;
        if (recovering() && inside_group && at(TokenKind::Newline) && layout_depth == 0 &&
            !at_next(TokenKind::Indent)) {
            throw CompileError(current().location, "unfinished expression group before end of line",
                               "dudu.parser.unfinished_group");
        }
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
            joined.has_layout_tokens = true;
            if (at(TokenKind::Indent)) {
                ++layout_depth;
            } else if (at(TokenKind::Dedent)) {
                layout_depth = std::max(0, layout_depth - 1);
            }
            ++cursor_;
            continue;
        }
        const Token& token = current();
        if (!joined.has_tokens) {
            joined.range.start = token.location;
            joined.has_tokens = true;
        }
        if (token.kind == TokenKind::Newline || token.kind == TokenKind::Indent ||
            token.kind == TokenKind::Dedent) {
            joined.has_layout_tokens = true;
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
    if (begin < joined.end && begin < tokens_.size()) {
        const Token& first = tokens_[begin];
        const Token& last = tokens_[joined.end - 1];
        if (first.kind != TokenKind::Newline && first.kind != TokenKind::Indent &&
            first.kind != TokenKind::Dedent && last.kind != TokenKind::Newline &&
            last.kind != TokenKind::Indent && last.kind != TokenKind::Dedent &&
            first.location.line == last.location.line) {
            joined.has_tokens = true;
            joined.range.start = first.location;
            joined.range.end = token_end_location(last);
            return joined;
        }
    }
    for (size_t index = begin; index < end && index < tokens_.size(); ++index) {
        const Token& token = tokens_[index];
        if (!joined.has_tokens) {
            joined.range.start = token.location;
            joined.has_tokens = true;
        }
        if (token.kind == TokenKind::Newline || token.kind == TokenKind::Indent ||
            token.kind == TokenKind::Dedent) {
            joined.has_layout_tokens = true;
        }
        joined.range.end = token_end_location(token);
    }
    return joined;
}

std::string Parser::token_source_spelling(size_t begin, size_t end) const {
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
    const std::span<const Token> source_tokens =
        tokens_.subspan(piece.begin, piece.end - piece.begin);
    std::vector<Token> filtered_tokens;
    if (piece.has_layout_tokens) {
        filtered_tokens = syntax_piece_tokens(source_tokens);
    }
    ExprTokenParser parser(filtered_tokens.empty() ? source_tokens
                                                   : std::span<const Token>{filtered_tokens});
    Expr expr = parser.parse();
    if (expr.kind == ExprKind::Unknown) {
        const std::string spelling = trim_string(token_source_spelling(piece.begin, piece.end));
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
    const std::span<const Token> source_tokens =
        tokens_.subspan(piece.begin, piece.end - piece.begin);
    std::vector<Token> filtered_tokens;
    if (piece.has_layout_tokens) {
        filtered_tokens = syntax_piece_tokens(source_tokens);
    }
    TypeTokenParser parser(filtered_tokens.empty() ? source_tokens
                                                   : std::span<const Token>{filtered_tokens});
    TypeRef type = parser.parse();
    if (const TypeRef* malformed = malformed_type_node(type)) {
        std::string spelling = trim_string(token_source_spelling(piece.begin, piece.end));
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

ParseResult parse_module_recovering(std::span<const Token> tokens) {
    ParseResult result;
    result.module = Parser(tokens, &result.diagnostics).parse();
    return result;
}

ModuleAst parse_source(std::string_view source, const std::filesystem::path& file) {
    ModuleAst module = parse_module(lex_source(source, file));
    attach_leading_doc_comments(module, source);
    return module;
}

ParseResult parse_source_recovering(std::string_view source, const std::filesystem::path& file) {
    LexResult lexed = lex_source_recovering(source, file);
    ParseResult result = parse_module_recovering(lexed.tokens);
    result.diagnostics.insert(result.diagnostics.begin(),
                              std::make_move_iterator(lexed.diagnostics.begin()),
                              std::make_move_iterator(lexed.diagnostics.end()));
    attach_leading_doc_comments(result.module, source);
    return result;
}

} // namespace dudu
