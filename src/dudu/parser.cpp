#include "dudu/parser.hpp"

#include "dudu/lexer.hpp"
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
                require_no_decorators(decorators, "type declaration");
                parse_type_decl(previous(), module);
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
        if (match_identifier("as")) {
            import.alias = consume_identifier("expected alias after as").text;
        }
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
        klass.generic_params = parse_generic_params();
        if (match(TokenKind::LParen)) {
            if (!at(TokenKind::RParen)) {
                while (true) {
                    std::string base = join_until({TokenKind::Comma, TokenKind::RParen});
                    if (base.empty()) {
                        fail_current("expected base class name");
                    }
                    klass.base_classes.push_back(std::move(base));
                    if (match(TokenKind::Comma)) {
                        continue;
                    }
                    break;
                }
            }
            consume(TokenKind::RParen, "expected ) after base classes");
        }
        consume(TokenKind::Colon, "expected : after class name");
        consume(TokenKind::Newline, "expected newline after class header");
        if (!match(TokenKind::Indent)) {
            fail_current("expected indented class body");
        }
        std::vector<Decorator> member_decorators;
        while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
            if (at(TokenKind::Newline)) {
                ++cursor_;
                continue;
            }
            if (match(TokenKind::At)) {
                member_decorators.push_back(parse_decorator(previous()));
                continue;
            }
            const Visibility member_visibility = parse_visibility();
            if (match_identifier("def")) {
                klass.methods.push_back(
                    parse_function(previous(), member_visibility, member_decorators, klass.name));
                member_decorators.clear();
                continue;
            }
            if (is_all_caps_identifier(current())) {
                require_no_decorators(member_decorators, "class constant");
                klass.constants.push_back(parse_constant());
                continue;
            }
            if (member_visibility != Visibility::Default) {
                fail_current("expected def after class member visibility");
            }
            require_no_decorators(member_decorators, "field");
            FieldDecl field = parse_field();
            if (field.type_ref.kind == TypeKind::Static) {
                if (field.value.empty()) {
                    throw CompileError(field.location, "static field requires an initializer");
                }
                ConstDecl static_field;
                static_field.name = field.name;
                static_field.type =
                    field.type_ref.children.empty() ? field.type : field.type_ref.children[0].text;
                static_field.type_ref = field.type_ref.children.empty()
                                            ? parse_type_text(static_field.type, field.location)
                                            : field.type_ref.children[0];
                static_field.value = field.value;
                static_field.value_expr = field.value_expr;
                static_field.location = field.location;
                klass.static_fields.push_back(std::move(static_field));
            } else {
                klass.fields.push_back(std::move(field));
            }
        }
        require_no_decorators(member_decorators, "class body");
        consume(TokenKind::Dedent, "expected dedent after class body");
        return klass;
    }

    FieldDecl parse_field() {
        FieldDecl field;
        const Token& name = consume_identifier("expected field name");
        field.name = name.text;
        field.location = name.location;
        consume(TokenKind::Colon, "expected : after field name");
        field.type = join_until({TokenKind::Assign, TokenKind::Newline});
        if (field.type.empty()) {
            throw CompileError(name.location, "field requires a type");
        }
        field.type_ref = parse_type_text(field.type, name.location);
        if (match(TokenKind::Assign)) {
            field.value = join_until({TokenKind::Newline});
            field.value_expr = parse_expr_text(field.value, name.location);
        }
        consume(TokenKind::Newline, "expected newline after field");
        return field;
    }

    EnumDecl parse_enum(const Token& start) {
        EnumDecl en;
        en.location = start.location;
        en.name = consume_identifier("expected enum name").text;
        consume(TokenKind::Colon, "expected : after enum name");
        if (!at(TokenKind::Newline)) {
            en.underlying_type = join_until({TokenKind::Newline});
            en.underlying_type_ref = parse_type_text(en.underlying_type, start.location);
        }
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
            if (match(TokenKind::LParen)) {
                value.tuple_payload = true;
                if (!at(TokenKind::RParen)) {
                    size_t index = 0;
                    while (true) {
                        EnumPayloadField field;
                        field.name = "_" + std::to_string(index);
                        field.location = current().location;
                        field.type = join_until({TokenKind::Comma, TokenKind::RParen});
                        if (field.type.empty()) {
                            throw CompileError(field.location, "enum payload field requires a type");
                        }
                        field.type_ref = parse_type_text(field.type, field.location);
                        value.payload_fields.push_back(std::move(field));
                        ++index;
                        if (match(TokenKind::Comma)) {
                            continue;
                        }
                        break;
                    }
                }
                consume(TokenKind::RParen, "expected ) after enum payload fields");
            } else if (match(TokenKind::Colon)) {
                consume(TokenKind::Newline, "expected newline after enum payload header");
                if (!match(TokenKind::Indent)) {
                    fail_current("expected indented enum payload fields");
                }
                while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
                    if (match(TokenKind::Newline)) {
                        continue;
                    }
                    const FieldDecl field = parse_field();
                    value.payload_fields.push_back({.name = field.name,
                                                    .type = field.type,
                                                    .type_ref = field.type_ref,
                                                    .location = field.location});
                }
                consume(TokenKind::Dedent, "expected dedent after enum payload fields");
                en.values.push_back(std::move(value));
                continue;
            } else if (match(TokenKind::Assign)) {
                value.value = join_until({TokenKind::Newline});
                value.value_expr = parse_expr_text(value.value, name.location);
            }
            consume(TokenKind::Newline, "expected newline after enum value");
            en.values.push_back(std::move(value));
        }
        consume(TokenKind::Dedent, "expected dedent after enum body");
        return en;
    }

    void parse_type_decl(const Token& start, ModuleAst& module) {
        const Token& name = consume_identifier("expected type name");
        if (match(TokenKind::Assign)) {
            TypeAliasDecl alias;
            alias.location = start.location;
            alias.name = name.text;
            alias.type = join_until({TokenKind::Newline});
            alias.type_ref = parse_type_text(alias.type, start.location);
            consume(TokenKind::Newline, "expected newline after type alias");
            module.aliases.push_back(std::move(alias));
            return;
        }
        NativeTypeDecl type;
        type.location = name.location;
        type.name = name.text;
        consume(TokenKind::Newline, "expected = or newline after type name");
        module.native_types.push_back(std::move(type));
    }

    FunctionDecl parse_function(const Token& start, Visibility visibility,
                                const std::vector<Decorator>& decorators,
                                std::string_view receiver_type = {}) {
        FunctionDecl fn;
        fn.visibility = visibility;
        fn.decorators = decorators;
        fn.location = start.location;
        fn.name = consume_identifier("expected function name").text;
        fn.generic_params = parse_generic_params();
        consume(TokenKind::LParen, "expected ( after function name");
        skip_signature_separators();
        if (!at(TokenKind::RParen)) {
            parse_params(fn.params, receiver_type);
        }
        skip_signature_separators();
        consume(TokenKind::RParen, "expected ) after parameters");
        if (match(TokenKind::Arrow)) {
            fn.return_type = join_until({TokenKind::Colon});
            fn.return_type_ref = parse_type_text(fn.return_type, start.location);
        }
        consume(TokenKind::Colon, "expected : after function header");
        consume(TokenKind::Newline, "expected newline after function header");
        fn.statements = parse_statement_block();
        return fn;
    }

    std::vector<std::string> parse_generic_params() {
        std::vector<std::string> params;
        if (!match(TokenKind::LBracket)) {
            return params;
        }
        if (at(TokenKind::RBracket)) {
            fail_current("generic parameter list cannot be empty");
        }
        while (true) {
            params.push_back(consume_identifier("expected generic parameter name").text);
            if (match(TokenKind::Comma)) {
                continue;
            }
            break;
        }
        consume(TokenKind::RBracket, "expected ] after generic parameters");
        return params;
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
                param.type_ref = parse_type_text(param.type, name.location);
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
            param.type_ref = parse_type_text(param.type, name.location);
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
        constant.type_ref = parse_type_text(constant.type, name.location);
        consume(TokenKind::Assign, "expected = after constant type");
        constant.value = join_until({TokenKind::Newline});
        constant.value_expr = parse_expr_text(constant.value, name.location);
        consume(TokenKind::Newline, "expected newline after constant");
        return constant;
    }

    StaticAssertDecl parse_static_assert() {
        StaticAssertDecl assertion;
        const Token& start = consume_identifier("expected static_assert");
        assertion.location = start.location;
        assertion.expression = join_until({TokenKind::Newline});
        assertion.expression_expr = parse_expr_text(assertion.expression, start.location);
        consume(TokenKind::Newline, "expected newline after static_assert");
        return assertion;
    }

    std::vector<Stmt> parse_statement_block() {
        std::vector<Stmt> out;
        if (!match(TokenKind::Indent)) {
            return out;
        }
        while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
            if (match(TokenKind::Newline)) {
                continue;
            }
            const SourceLocation location = current().location;
            std::string text = join_until({TokenKind::Newline});
            const SourceRange range = range_for_line_text(location, text);
            consume(TokenKind::Newline, "expected newline after statement");
            std::vector<Stmt> children;
            if (at(TokenKind::Indent)) {
                children = parse_statement_block();
            }
            out.push_back(
                statement_from_text(std::move(text), location, range, std::move(children)));
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
        return out.str();
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
