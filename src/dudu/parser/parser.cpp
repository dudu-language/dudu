#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/parser/parser_doc_comments.hpp"
#include "dudu/parser/parser_internal.hpp"
#include "dudu/parser/parser_utils.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace dudu {
namespace {

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
                module.enums.push_back(parse_enum(previous(), decorators));
                decorators.clear();
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
                module.constants.push_back(parse_constant(decorators));
                decorators.clear();
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
    if (current().kind == TokenKind::Identifier &&
        (current().text == "c" || current().text == "cxx" || current().text == "cpp")) {
        const std::string language(current().text);
        throw CompileError(current().location, "native imports use `from " + language +
                                                   " import header` or `from " + language +
                                                   ".path import header`");
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
