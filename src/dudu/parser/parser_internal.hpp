#pragma once

#include "dudu/parser/parser.hpp"

#include <cstddef>
#include <initializer_list>
#include <string_view>

namespace dudu {

class Parser {
  public:
    explicit Parser(std::span<const Token> tokens);

    ModuleAst parse();

    struct JoinedTokens {
        SourceRange range;
        size_t begin = 0;
        size_t end = 0;
        bool has_tokens = false;
        bool has_layout_tokens = false;
    };

  private:
    std::span<const Token> tokens_;
    size_t cursor_ = 0;

    const Token& current() const;
    const Token& previous() const;
    bool at(TokenKind kind) const;
    bool check_text(std::string_view text) const;
    template <size_t N> bool check_text(const char (&text)[N]) const {
        return token_text_is(current(), TokenKind::Identifier, text);
    }
    bool match(TokenKind kind);
    bool match_identifier(std::string_view text);
    template <size_t N> bool match_identifier(const char (&text)[N]) {
        if (!check_text(text)) {
            return false;
        }
        ++cursor_;
        return true;
    }
    const Token& consume(TokenKind kind, std::string_view message);
    const Token& consume_identifier(std::string_view message);
    [[noreturn]] void fail_current(const std::string& message) const;

    void skip_newlines();
    void require_no_decorators(const std::vector<Decorator>& decorators,
                               std::string_view target) const;
    Visibility parse_visibility();
    Decorator parse_decorator(const Token& at_token);
    ImportDecl parse_import(const Token& start);
    ImportDecl parse_foreign_import(const Token& start, ImportKind kind, size_t statement_begin);
    ImportDecl parse_from_import(const Token& start);
    std::string parse_path();

    ClassDecl parse_class(const Token& start, Visibility visibility,
                          const std::vector<Decorator>& decorators);
    FieldDecl parse_field();
    EnumDecl parse_enum(const Token& start);
    void parse_type_decl(const Token& start, ModuleAst& module);
    FunctionDecl parse_function(const Token& start, Visibility visibility,
                                const std::vector<Decorator>& decorators,
                                std::string_view receiver_type = {});
    std::vector<std::string> parse_generic_params();
    void parse_params(std::vector<ParamDecl>& params, const TypeRef& receiver_type);
    void skip_signature_separators();
    ConstDecl parse_constant();
    StaticAssertDecl parse_static_assert();

    std::vector<Stmt> parse_statement_block();
    Stmt parse_statement(std::vector<Stmt> children, size_t statement_end);
    bool starts_statement_continuation(size_t cursor) const;
    size_t consume_statement_continuation_block();
    JoinedTokens join_until_with_range(std::initializer_list<TokenKind> stops);
    JoinedTokens join_tokens(size_t begin, size_t end) const;
    std::string token_source_spelling(size_t begin, size_t end) const;
    Expr parse_expr_piece(const JoinedTokens& piece) const;
    TypeRef parse_type_piece(const JoinedTokens& piece) const;
    std::vector<JoinedTokens> split_top_level_comma_pieces(const JoinedTokens& piece) const;
    JoinedTokens join_until_top_level_identifier(std::string_view identifier,
                                                 std::initializer_list<TokenKind> stops);
    size_t estimate_statement_block_capacity() const;
};

} // namespace dudu
