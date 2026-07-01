#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/parser/parser_doc_comments.hpp"
#include "dudu/parser/parser_internal.hpp"
#include "dudu/parser/parser_utils.hpp"

namespace dudu {

namespace {

Visibility visibility_from_name(Visibility explicit_visibility, const std::string& name) {
    if (explicit_visibility != Visibility::Default) {
        return explicit_visibility;
    }
    return name.size() > 1 && name.front() == '_' ? Visibility::Private : Visibility::Default;
}

TypeRef bare_self_type_ref(SourceLocation location) {
    TypeRef self = named_type_ref("Self", location);
    return wrapped_type_ref(TypeKind::Reference, std::move(self), location);
}

} // namespace

ClassDecl Parser::parse_class(const Token& start, Visibility visibility,
                              const std::vector<Decorator>& decorators) {
    (void)start;
    ClassDecl klass;
    klass.visibility = visibility;
    klass.decorators = decorators;
    const Token& name = consume_identifier("expected class name");
    klass.location = name.location;
    klass.name = name.text;
    klass.generic_params = parse_generic_params();
    if (match(TokenKind::LParen)) {
        if (!at(TokenKind::RParen)) {
            while (true) {
                const JoinedTokens base =
                    join_until_with_range({TokenKind::Comma, TokenKind::RParen});
                if (!base.has_tokens) {
                    fail_current("expected base class name");
                }
                BaseClassDecl base_decl;
                base_decl.type_ref = parse_type_piece(base);
                base_decl.location = base.range.start;
                klass.base_class_refs.push_back(std::move(base_decl));
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
    bool can_accept_docstring = true;
    while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
        if (at(TokenKind::Newline)) {
            match(TokenKind::Newline);
            continue;
        }
        if (can_accept_docstring && at(TokenKind::String)) {
            require_no_decorators(member_decorators, "class docstring");
            const JoinedTokens doc = join_tokens(cursor_, cursor_ + 1);
            const Expr expr = parse_expr_piece(doc);
            klass.doc_comment = normalize_docstring_text(expr.value);
            ++cursor_;
            consume(TokenKind::Newline, "expected newline after class docstring");
            can_accept_docstring = false;
            continue;
        }
        can_accept_docstring = false;
        if (at(TokenKind::String)) {
            throw CompileError(current().location,
                               "misplaced docstring; class docstrings must be the first "
                               "statement in the class body",
                               "dudu.parser.misplaced_docstring");
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
        require_no_decorators(member_decorators, "field");
        FieldDecl field = parse_field();
        if (field.type_ref.kind == TypeKind::Static) {
            if (expr_missing(field.value_expr)) {
                throw CompileError(field.location, "static field requires an initializer");
            }
            ConstDecl static_field;
            static_field.name = field.name;
            if (field.type_ref.children.empty()) {
                throw CompileError(field.location,
                                   "malformed static field type: missing child type");
            }
            static_field.type_ref = field.type_ref.children[0];
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

FieldDecl Parser::parse_field() {
    FieldDecl field;
    const Token& name = consume_identifier("expected field name");
    field.name = name.text;
    field.location = name.location;
    consume(TokenKind::Colon, "expected : after field name");
    const JoinedTokens type = join_until_with_range({TokenKind::Assign, TokenKind::Newline});
    if (!type.has_tokens) {
        throw CompileError(name.location, "field requires a type");
    }
    field.type_ref = parse_type_piece(type);
    if (match(TokenKind::Assign)) {
        const JoinedTokens value = join_until_with_range({TokenKind::Newline});
        field.value_expr = parse_expr_piece(value);
    }
    consume(TokenKind::Newline, "expected newline after field");
    return field;
}

EnumDecl Parser::parse_enum(const Token& start) {
    (void)start;
    EnumDecl en;
    const Token& name = consume_identifier("expected enum name");
    en.location = name.location;
    en.name = name.text;
    consume(TokenKind::Colon, "expected : after enum name");
    if (!at(TokenKind::Newline)) {
        const JoinedTokens type = join_until_with_range({TokenKind::Newline});
        en.underlying_type_ref = parse_type_piece(type);
    }
    consume(TokenKind::Newline, "expected newline after enum header");
    if (!match(TokenKind::Indent)) {
        fail_current("expected indented enum body");
    }
    bool can_accept_docstring = true;
    while (!at(TokenKind::Dedent) && !at(TokenKind::End)) {
        if (match(TokenKind::Newline)) {
            continue;
        }
        if (can_accept_docstring && at(TokenKind::String)) {
            const JoinedTokens doc = join_tokens(cursor_, cursor_ + 1);
            const Expr expr = parse_expr_piece(doc);
            en.doc_comment = normalize_docstring_text(expr.value);
            ++cursor_;
            consume(TokenKind::Newline, "expected newline after enum docstring");
            can_accept_docstring = false;
            continue;
        }
        can_accept_docstring = false;
        if (at(TokenKind::String)) {
            throw CompileError(current().location,
                               "misplaced docstring; enum docstrings must be the first "
                               "statement in the enum body",
                               "dudu.parser.misplaced_docstring");
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
                    const JoinedTokens type =
                        join_until_with_range({TokenKind::Comma, TokenKind::RParen});
                    if (!type.has_tokens) {
                        throw CompileError(field.location, "enum payload field requires a type");
                    }
                    field.type_ref = parse_type_piece(type);
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
                value.payload_fields.push_back(
                    {.name = field.name, .type_ref = field.type_ref, .location = field.location});
            }
            consume(TokenKind::Dedent, "expected dedent after enum payload fields");
            en.values.push_back(std::move(value));
            continue;
        } else if (match(TokenKind::Assign)) {
            const JoinedTokens expr = join_until_with_range({TokenKind::Newline});
            value.value_expr = parse_expr_piece(expr);
        }
        consume(TokenKind::Newline, "expected newline after enum value");
        en.values.push_back(std::move(value));
    }
    consume(TokenKind::Dedent, "expected dedent after enum body");
    return en;
}

void Parser::parse_type_decl(const Token& start, ModuleAst& module) {
    (void)start;
    const Token& name = consume_identifier("expected type name");
    if (match(TokenKind::Assign)) {
        TypeAliasDecl alias;
        alias.location = name.location;
        alias.name = name.text;
        const JoinedTokens type = join_until_with_range({TokenKind::Newline});
        alias.type_ref = parse_type_piece(type);
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

FunctionDecl Parser::parse_function(const Token& start, Visibility visibility,
                                    const std::vector<Decorator>& decorators,
                                    std::string_view receiver_type) {
    (void)start;
    FunctionDecl fn;
    fn.visibility = visibility;
    fn.decorators = decorators;
    const Token& name = consume_identifier("expected function name");
    SourceLocation name_location = name.location;
    fn.name = name.text;
    if (receiver_type.empty() && match(TokenKind::Dot)) {
        fn.receiver_type_ref = named_type_ref(fn.name, name.location);
        const Token& method_name = consume_identifier("expected method name after .");
        name_location = method_name.location;
        fn.name = method_name.text;
    } else {
        if (!receiver_type.empty()) {
            fn.receiver_type_ref = named_type_ref(std::string(receiver_type), name.location);
        }
    }
    fn.location = name_location;
    fn.visibility = visibility_from_name(fn.visibility, fn.name);
    fn.generic_params = parse_generic_params();
    consume(TokenKind::LParen, "expected ( after function name");
    skip_signature_separators();
    if (!at(TokenKind::RParen)) {
        parse_params(fn.params, fn.receiver_type_ref);
    }
    skip_signature_separators();
    consume(TokenKind::RParen, "expected ) after parameters");
    if (match(TokenKind::Arrow)) {
        const JoinedTokens type = join_until_with_range({TokenKind::Colon, TokenKind::Newline});
        fn.return_type_ref = parse_type_piece(type);
    }
    consume(TokenKind::Colon, "expected : after function header");
    consume(TokenKind::Newline, "expected newline after function header");
    fn.statements = parse_statement_block();
    return fn;
}

std::vector<std::string> Parser::parse_generic_params() {
    std::vector<std::string> params;
    if (!match(TokenKind::LBracket)) {
        return params;
    }
    if (at(TokenKind::RBracket)) {
        fail_current("generic parameter list cannot be empty");
    }
    while (true) {
        std::string param = std::string(consume_identifier("expected generic parameter name").text);
        if (match(TokenKind::Ellipsis)) {
            param += "...";
        }
        params.push_back(std::move(param));
        if (match(TokenKind::Comma)) {
            continue;
        }
        break;
    }
    consume(TokenKind::RBracket, "expected ] after generic parameters");
    return params;
}

void Parser::parse_params(std::vector<ParamDecl>& params, const TypeRef& receiver_type) {
    while (true) {
        skip_signature_separators();
        if (at(TokenKind::RParen)) {
            break;
        }
        ParamDecl param;
        if (at(TokenKind::Operator) && current().text == "*") {
            param.variadic = true;
            ++cursor_;
        }
        const Token& name = consume_identifier("expected parameter name");
        param.name = name.text;
        param.location = name.location;
        if (has_type_ref(receiver_type) && params.empty() && param.name == "self" &&
            !at(TokenKind::Colon)) {
            param.type_ref = bare_self_type_ref(name.location);
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
        const JoinedTokens type =
            join_until_with_range({TokenKind::Comma, TokenKind::RParen, TokenKind::Newline});
        if (!type.has_tokens) {
            throw CompileError(name.location, "parameter requires a type");
        }
        param.type_ref = parse_type_piece(type);
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

void Parser::skip_signature_separators() {
    while (at(TokenKind::Newline) || at(TokenKind::Indent) || at(TokenKind::Dedent)) {
        match(current().kind);
    }
}

ConstDecl Parser::parse_constant() {
    ConstDecl constant;
    const Token& name = consume_identifier("expected constant name");
    constant.name = name.text;
    constant.location = name.location;
    consume(TokenKind::Colon, "expected : after constant name");
    const JoinedTokens type = join_until_with_range({TokenKind::Assign});
    if (!type.has_tokens) {
        throw CompileError(name.location, "constant requires a type");
    }
    constant.type_ref = parse_type_piece(type);
    consume(TokenKind::Assign, "expected = after constant type");
    const JoinedTokens value = join_until_with_range({TokenKind::Newline});
    constant.value_expr = parse_expr_piece(value);
    consume(TokenKind::Newline, "expected newline after constant");
    return constant;
}

StaticAssertDecl Parser::parse_static_assert() {
    StaticAssertDecl assertion;
    const Token& start = consume_identifier("expected static_assert");
    assertion.location = start.location;
    const JoinedTokens expression = join_until_with_range({TokenKind::Newline});
    assertion.expression_expr = parse_expr_piece(expression);
    consume(TokenKind::Newline, "expected newline after static_assert");
    return assertion;
}

} // namespace dudu
