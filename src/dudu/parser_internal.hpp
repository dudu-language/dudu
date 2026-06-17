#pragma once

#include "dudu/parser.hpp"

#include <initializer_list>
#include <string_view>

namespace dudu {

class Parser {
  public:
    explicit Parser(std::span<const Token> tokens);

    ModuleAst parse();

    struct JoinedTokens {
        std::string text;
        std::string source_text;
        SourceRange range;
        bool has_tokens = false;
    };

  private:
    std::span<const Token> tokens_;
    size_t cursor_ = 0;

    const Token& current() const;
    const Token& previous() const;
    bool at(TokenKind kind) const;
    bool check_text(std::string_view text) const;
    bool match(TokenKind kind);
    bool match_identifier(std::string_view text);
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
    void parse_params(std::vector<ParamDecl>& params, std::string_view receiver_type);
    void skip_signature_separators();
    ConstDecl parse_constant();
    StaticAssertDecl parse_static_assert();

    std::vector<Stmt> parse_statement_block();
    Stmt parse_statement(std::vector<Stmt> children, size_t statement_end);
    std::string join_until(std::initializer_list<TokenKind> stops);
    JoinedTokens join_until_with_range(std::initializer_list<TokenKind> stops);
    JoinedTokens join_tokens(size_t begin, size_t end) const;
    JoinedTokens join_until_top_level_identifier(std::string_view identifier,
                                                 std::initializer_list<TokenKind> stops);
};

} // namespace dudu
