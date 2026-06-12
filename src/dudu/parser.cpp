#include "dudu/parser.hpp"

#include "dudu/lexer.hpp"

#include <cctype>
#include <map>
#include <sstream>

namespace dudu {
namespace {
class Parser {
  public:
    explicit Parser(std::span<const Token> tokens) : tokens_(tokens) {
    }

    ModuleAst parse() {
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
                require_no_decorators(decorators, "type alias");
                module.aliases.push_back(parse_type_alias(previous()));
            } else if (match_identifier("def")) {
                module.functions.push_back(parse_function(previous(), visibility, decorators));
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

  private:
    std::span<const Token> tokens_;
    size_t cursor_ = 0;

    const Token& current() const {
        return tokens_[cursor_];
    }
    const Token& previous() const {
        return tokens_[cursor_ - 1];
    }
    bool at(TokenKind kind) const {
        return current().kind == kind;
    }
    bool check_text(std::string_view text) const {
        return current().kind == TokenKind::Identifier && current().text == text;
    }
    bool match(TokenKind kind) {
        if (!at(kind)) {
            return false;
        }
        ++cursor_;
        return true;
    }

    bool match_identifier(std::string_view text) {
        if (!check_text(text)) {
            return false;
        }
        ++cursor_;
        return true;
    }

    const Token& consume(TokenKind kind, std::string_view message) {
        if (!at(kind)) {
            fail_current(std::string(message));
        }
        return tokens_[cursor_++];
    }

    const Token& consume_identifier(std::string_view message) {
        return consume(TokenKind::Identifier, message);
    }

    [[noreturn]] void fail_current(const std::string& message) const {
        throw CompileError(current().location, message);
    }

    void skip_newlines() {
        while (match(TokenKind::Newline)) {
        }
    }

    static bool is_all_caps_identifier(const Token& token) {
        if (token.kind != TokenKind::Identifier || token.text.empty()) {
            return false;
        }
        bool saw_letter = false;
        for (char c : token.text) {
            if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
                saw_letter = true;
                if (std::islower(static_cast<unsigned char>(c)) != 0) {
                    return false;
                }
            } else if (std::isdigit(static_cast<unsigned char>(c)) == 0 && c != '_') {
                return false;
            }
        }
        return saw_letter;
    }

    void require_no_decorators(const std::vector<Decorator>& decorators,
                               std::string_view target) const {
        if (!decorators.empty()) {
            throw CompileError(decorators.front().location,
                               "decorators are not valid before " + std::string(target));
        }
    }

    Visibility parse_visibility() {
        if (match_identifier("public")) {
            return Visibility::Public;
        }
        if (match_identifier("private")) {
            return Visibility::Private;
        }
        return Visibility::Default;
    }

    Decorator parse_decorator(const Token& at_token) {
        std::string text = join_until({TokenKind::Newline});
        consume(TokenKind::Newline, "expected newline after decorator");
        return {.text = std::move(text), .location = at_token.location};
    }

    ImportDecl parse_import(const Token& start) {
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

    ImportDecl parse_foreign_import(const Token& start, ImportKind kind) {
        ImportDecl import;
        import.kind = kind;
        import.location = start.location;
        import.module_path = consume(TokenKind::String, "expected quoted foreign header").text;
        if (!match_identifier("as")) {
            fail_current("foreign import requires as alias");
        }
        import.alias = consume_identifier("expected alias after as").text;
        consume(TokenKind::Newline, "expected newline after foreign import");
        return import;
    }

    ImportDecl parse_from_import(const Token& start) {
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

    std::string parse_path() {
        std::string out = consume_identifier("expected module path").text;
        while (match(TokenKind::Dot)) {
            out += '.';
            out += consume_identifier("expected name after dot").text;
        }
        return out;
    }

    ClassDecl parse_class(const Token& start, Visibility visibility,
                          const std::vector<Decorator>& decorators) {
        ClassDecl klass;
        klass.visibility = visibility;
        klass.decorators = decorators;
        klass.location = start.location;
        klass.name = consume_identifier("expected class name").text;
        consume(TokenKind::Colon, "expected : after class name");
        consume(TokenKind::Newline, "expected newline after class header");
        if (!match(TokenKind::Indent)) {
            fail_current("expected indented class body");
        }
        while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
            if (at(TokenKind::Newline)) {
                ++cursor_;
                continue;
            }
            if (match_identifier("def")) {
                klass.methods.push_back(
                    parse_function(previous(), Visibility::Default, {}, klass.name));
                continue;
            }
            klass.fields.push_back(parse_field());
        }
        consume(TokenKind::Dedent, "expected dedent after class body");
        return klass;
    }

    FieldDecl parse_field() {
        FieldDecl field;
        const Token& name = consume_identifier("expected field name");
        field.name = name.text;
        field.location = name.location;
        consume(TokenKind::Colon, "expected : after field name");
        field.type = join_until({TokenKind::Newline});
        if (field.type.empty()) {
            throw CompileError(name.location, "field requires a type");
        }
        consume(TokenKind::Newline, "expected newline after field");
        return field;
    }

    EnumDecl parse_enum(const Token& start) {
        EnumDecl en;
        en.location = start.location;
        en.name = consume_identifier("expected enum name").text;
        if (match(TokenKind::LParen)) {
            en.underlying_type = join_until({TokenKind::RParen});
            consume(TokenKind::RParen, "expected ) after enum underlying type");
        }
        consume(TokenKind::Colon, "expected : after enum name");
        consume(TokenKind::Newline, "expected newline after enum header");
        if (!match(TokenKind::Indent)) {
            fail_current("expected indented enum body");
        }
        while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
            if (match(TokenKind::Newline)) {
                continue;
            }
            EnumValueDecl value;
            const Token& name = consume_identifier("expected enum value");
            value.name = name.text;
            value.location = name.location;
            if (match(TokenKind::Assign)) {
                value.value = join_until({TokenKind::Newline});
            }
            consume(TokenKind::Newline, "expected newline after enum value");
            en.values.push_back(std::move(value));
        }
        consume(TokenKind::Dedent, "expected dedent after enum body");
        return en;
    }

    TypeAliasDecl parse_type_alias(const Token& start) {
        TypeAliasDecl alias;
        alias.location = start.location;
        alias.name = consume_identifier("expected type alias name").text;
        consume(TokenKind::Assign, "expected = after type alias name");
        alias.type = join_until({TokenKind::Newline});
        consume(TokenKind::Newline, "expected newline after type alias");
        return alias;
    }

    FunctionDecl parse_function(const Token& start, Visibility visibility,
                                const std::vector<Decorator>& decorators,
                                std::string_view receiver_type = {}) {
        FunctionDecl fn;
        fn.visibility = visibility;
        fn.decorators = decorators;
        fn.location = start.location;
        fn.name = consume_identifier("expected function name").text;
        consume(TokenKind::LParen, "expected ( after function name");
        skip_signature_separators();
        if (!at(TokenKind::RParen)) {
            parse_params(fn.params, receiver_type);
        }
        skip_signature_separators();
        consume(TokenKind::RParen, "expected ) after parameters");
        if (match(TokenKind::Arrow)) {
            fn.return_type = join_until({TokenKind::Colon});
        }
        consume(TokenKind::Colon, "expected : after function header");
        consume(TokenKind::Newline, "expected newline after function header");
        fn.body = parse_raw_block();
        return fn;
    }

    void parse_params(std::vector<ParamDecl>& params, std::string_view receiver_type) {
        while (true) {
            skip_signature_separators();
            if (at(TokenKind::RParen)) {
                break;
            }
            ParamDecl param;
            const Token& name = consume_identifier("expected parameter name");
            param.name = name.text;
            param.location = name.location;
            if (!receiver_type.empty() && params.empty() && param.name == "self" &&
                !at(TokenKind::Colon)) {
                param.type = std::string(receiver_type);
                params.push_back(std::move(param));
                if (match(TokenKind::Comma)) {
                    continue;
                }
                if (at(TokenKind::RParen)) {
                    break;
                }
                fail_current("expected comma after self");
            }
            consume(TokenKind::Colon, "expected : after parameter name");
            param.type = join_until({TokenKind::Comma, TokenKind::RParen, TokenKind::Newline});
            if (param.type.empty()) {
                throw CompileError(name.location, "parameter requires a type");
            }
            params.push_back(std::move(param));
            if (match(TokenKind::Comma)) {
                continue;
            }
            if (at(TokenKind::Newline)) {
                continue;
            }
            if (at(TokenKind::RParen)) {
                break;
            }
            fail_current("expected comma or ) after parameter");
        }
    }

    void skip_signature_separators() {
        while (at(TokenKind::Newline) || at(TokenKind::Indent) || at(TokenKind::Dedent)) {
            ++cursor_;
        }
    }

    ConstDecl parse_constant() {
        ConstDecl constant;
        const Token& name = consume_identifier("expected constant name");
        constant.name = name.text;
        constant.location = name.location;
        consume(TokenKind::Colon, "expected : after constant name");
        constant.type = join_until({TokenKind::Assign});
        if (constant.type.empty()) {
            throw CompileError(name.location, "constant requires a type");
        }
        consume(TokenKind::Assign, "expected = after constant type");
        constant.value = join_until({TokenKind::Newline});
        consume(TokenKind::Newline, "expected newline after constant");
        return constant;
    }

    StaticAssertDecl parse_static_assert() {
        StaticAssertDecl assertion;
        const Token& start = consume_identifier("expected static_assert");
        assertion.location = start.location;
        assertion.expression = join_until({TokenKind::Newline});
        consume(TokenKind::Newline, "expected newline after static_assert");
        return assertion;
    }

    std::vector<RawStmt> parse_raw_block() {
        std::vector<RawStmt> out;
        if (!match(TokenKind::Indent)) {
            return out;
        }
        while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
            if (match(TokenKind::Newline)) {
                continue;
            }
            RawStmt stmt;
            stmt.location = current().location;
            stmt.text = join_until({TokenKind::Newline});
            consume(TokenKind::Newline, "expected newline after statement");
            if (at(TokenKind::Indent)) {
                stmt.children = parse_raw_block();
            }
            out.push_back(std::move(stmt));
        }
        consume(TokenKind::Dedent, "expected dedent after block");
        return out;
    }

    std::string join_until(std::initializer_list<TokenKind> stops) {
        std::ostringstream out;
        bool first = true;
        TokenKind previous_kind = TokenKind::End;
        int bracket_depth = 0;
        int paren_depth = 0;
        while (!at(TokenKind::End)) {
            if (bracket_depth == 0 && paren_depth == 0) {
                bool stop = false;
                for (TokenKind kind : stops) {
                    stop = stop || at(kind);
                }
                if (stop) {
                    break;
                }
            }
            if (!first && needs_space_between(previous_kind, current().kind)) {
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
            }
            ++cursor_;
        }
        return out.str();
    }

    static bool needs_space_between(TokenKind previous, TokenKind current) {
        if (current == TokenKind::Comma || current == TokenKind::Colon ||
            current == TokenKind::RParen || current == TokenKind::RBracket ||
            current == TokenKind::Dot || current == TokenKind::LParen ||
            current == TokenKind::LBracket) {
            return false;
        }
        if (previous == TokenKind::Dot || previous == TokenKind::LParen ||
            previous == TokenKind::LBracket) {
            return false;
        }
        return true;
    }

    static bool is_foreign_import(const ImportDecl& import) {
        return import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp;
    }
    static void validate_import_bindings(const std::vector<ImportDecl>& imports) {
        std::map<std::string, ImportDecl> direct;
        for (const ImportDecl& import : imports) {
            if (import.kind == ImportKind::Module && import.alias.empty()) {
                continue;
            }
            const std::string name = bound_import_name(import);
            const auto [it, inserted] = direct.emplace(name, import);
            if (!inserted) {
                if (is_foreign_import(import) && is_foreign_import(it->second)) {
                    continue;
                }
                throw CompileError(import.location, "import name '" + name +
                                                        "' collides with an earlier direct import");
            }
        }
    }
};

} // namespace
ModuleAst parse_module(std::span<const Token> tokens) {
    return Parser(tokens).parse();
}

ModuleAst parse_source(std::string_view source, const std::filesystem::path& file) {
    return parse_module(lex_source(source, file));
}

} // namespace dudu
