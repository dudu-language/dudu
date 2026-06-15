#include "dudu/language_server_semantic_tokens.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

struct SemanticToken {
    int line = 0;
    int column = 0;
    int length = 0;
    int type = 0;
    int modifiers = 0;
};

void add_semantic_token(std::vector<SemanticToken>& tokens, const SourceLocation& loc,
                        std::string_view text, int type, int modifiers = 0) {
    if (text.empty() || loc.line <= 0 || loc.column <= 0) {
        return;
    }
    tokens.push_back({.line = loc.line - 1,
                      .column = loc.column - 1,
                      .length = static_cast<int>(text.size()),
                      .type = type,
                      .modifiers = modifiers});
}

SourceLocation shifted_location(SourceLocation loc, int columns) {
    loc.column += columns;
    return loc;
}

void collect_type_tokens(const TypeRef& type, std::vector<SemanticToken>& tokens) {
    if (type.kind == TypeKind::Named || type.kind == TypeKind::Qualified ||
        type.kind == TypeKind::Template) {
        const std::string_view label = type.name.empty() ? type.text : type.name;
        add_semantic_token(tokens, type.location, label, 1);
    }
    for (const TypeRef& child : type.children) {
        collect_type_tokens(child, tokens);
    }
}

void collect_expr_tokens(const Expr& expr, std::vector<SemanticToken>& tokens);

void collect_call_callee_tokens(const Expr& expr, std::vector<SemanticToken>& tokens) {
    if (expr.kind == ExprKind::Name) {
        add_semantic_token(tokens, expr.location, expr.name, 4);
        return;
    }
    if (expr.kind == ExprKind::Member) {
        for (const Expr& child : expr.children) {
            collect_expr_tokens(child, tokens);
        }
        const SourceLocation member_location{
            .file = expr.location.file,
            .line = expr.location.line,
            .column = expr.location.column + static_cast<int>(expr.text.size() - expr.name.size())};
        add_semantic_token(tokens, member_location, expr.name, 4);
        return;
    }
    collect_expr_tokens(expr, tokens);
}

void collect_expr_tokens(const Expr& expr, std::vector<SemanticToken>& tokens) {
    switch (expr.kind) {
    case ExprKind::Name:
        add_semantic_token(tokens, expr.location, expr.name, 6);
        break;
    case ExprKind::Call:
    case ExprKind::TemplateCall:
        if (!expr.callee.empty()) {
            collect_call_callee_tokens(expr.callee.front(), tokens);
        } else {
            add_semantic_token(tokens, expr.location, expr.name, 4);
        }
        break;
    case ExprKind::Member: {
        const SourceLocation member_location{
            .file = expr.location.file,
            .line = expr.location.line,
            .column = expr.location.column + static_cast<int>(expr.text.size() - expr.name.size())};
        add_semantic_token(tokens, member_location, expr.name, 8);
        break;
    }
    case ExprKind::NamedArg:
        add_semantic_token(tokens, expr.location, expr.name, 9);
        break;
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
        add_semantic_token(tokens, expr.location, expr.text, 12);
        break;
    case ExprKind::StringLiteral:
        add_semantic_token(tokens, expr.location, expr.text, 13);
        break;
    default:
        break;
    }
    if (expr.kind != ExprKind::Call && expr.kind != ExprKind::TemplateCall) {
        for (const Expr& callee : expr.callee) {
            collect_expr_tokens(callee, tokens);
        }
    }
    for (const Expr& param : expr.params) {
        collect_expr_tokens(param, tokens);
    }
    for (const Expr& child : expr.children) {
        collect_expr_tokens(child, tokens);
    }
    if (!expr.template_type_args.empty()) {
        for (const TypeRef& arg : expr.template_type_args) {
            collect_type_tokens(arg, tokens);
        }
    } else {
        for (const Expr& arg : expr.template_args) {
            collect_expr_tokens(arg, tokens);
        }
    }
}

void collect_stmt_tokens(const std::vector<Stmt>& statements, std::vector<SemanticToken>& tokens) {
    for (const Stmt& stmt : statements) {
        if (stmt.kind == StmtKind::VarDecl) {
            add_semantic_token(tokens, stmt.location, stmt.name, 6, 1);
            collect_type_tokens(stmt.type_ref, tokens);
            collect_expr_tokens(stmt.value_expr, tokens);
        } else if (stmt.kind == StmtKind::Assign || stmt.kind == StmtKind::CompoundAssign) {
            collect_expr_tokens(stmt.target_expr, tokens);
            collect_expr_tokens(stmt.value_expr, tokens);
        } else if (stmt.kind == StmtKind::Return || stmt.kind == StmtKind::Raise ||
                   stmt.kind == StmtKind::Delete) {
            collect_expr_tokens(stmt.value_expr, tokens);
        } else if (stmt.kind == StmtKind::If || stmt.kind == StmtKind::Elif ||
                   stmt.kind == StmtKind::While || stmt.kind == StmtKind::Assert ||
                   stmt.kind == StmtKind::DebugAssert) {
            collect_expr_tokens(stmt.condition_expr, tokens);
        } else if (stmt.kind == StmtKind::For) {
            add_semantic_token(tokens, stmt.location, stmt.name, 6, 1);
            collect_type_tokens(stmt.type_ref, tokens);
            collect_expr_tokens(stmt.iterable_expr, tokens);
        } else if (stmt.kind == StmtKind::Expr) {
            collect_expr_tokens(stmt.expr, tokens);
        }
        collect_stmt_tokens(stmt.children, tokens);
    }
}

void collect_semantic_tokens(const ModuleAst& module, std::vector<SemanticToken>& tokens) {
    for (const ImportDecl& import : module.imports) {
        add_semantic_token(tokens, shifted_location(import.location, 7), bound_import_name(import),
                           0);
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        add_semantic_token(tokens, shifted_location(alias.location, 5), alias.name, 1, 1);
        collect_type_tokens(alias.type_ref, tokens);
    }
    for (const EnumDecl& en : module.enums) {
        add_semantic_token(tokens, shifted_location(en.location, 5), en.name, 3, 1);
        collect_type_tokens(en.underlying_type_ref, tokens);
        for (const EnumValueDecl& value : en.values) {
            add_semantic_token(tokens, value.location, value.name, 9, 1);
            collect_expr_tokens(value.value_expr, tokens);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        add_semantic_token(tokens, shifted_location(klass.location, 6), klass.name, 2, 1);
        for (const FieldDecl& field : klass.fields) {
            add_semantic_token(tokens, field.location, field.name, 8, 1);
            collect_type_tokens(field.type_ref, tokens);
            collect_expr_tokens(field.value_expr, tokens);
        }
        for (const ConstDecl& constant : klass.constants) {
            add_semantic_token(tokens, constant.location, constant.name, 8, 1 | 4);
            collect_type_tokens(constant.type_ref, tokens);
            collect_expr_tokens(constant.value_expr, tokens);
        }
        for (const ConstDecl& field : klass.static_fields) {
            add_semantic_token(tokens, field.location, field.name, 8, 1 | 8);
            collect_type_tokens(field.type_ref, tokens);
            collect_expr_tokens(field.value_expr, tokens);
        }
        for (const FunctionDecl& method : klass.methods) {
            add_semantic_token(tokens, shifted_location(method.location, 4), method.name, 5, 1);
            for (const ParamDecl& param : method.params) {
                add_semantic_token(tokens, param.location, param.name, 7, 1);
                collect_type_tokens(param.type_ref, tokens);
            }
            collect_type_tokens(method.return_type_ref, tokens);
            collect_stmt_tokens(method.statements, tokens);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        add_semantic_token(tokens, constant.location, constant.name, 6, 1 | 4);
        collect_type_tokens(constant.type_ref, tokens);
        collect_expr_tokens(constant.value_expr, tokens);
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        collect_expr_tokens(assertion.expression_expr, tokens);
    }
    for (const FunctionDecl& fn : module.functions) {
        add_semantic_token(tokens, shifted_location(fn.location, 4), fn.name, 4, 1);
        for (const ParamDecl& param : fn.params) {
            add_semantic_token(tokens, param.location, param.name, 7, 1);
            collect_type_tokens(param.type_ref, tokens);
        }
        collect_type_tokens(fn.return_type_ref, tokens);
        collect_stmt_tokens(fn.statements, tokens);
    }
}

} // namespace

std::string semantic_tokens_json(const ModuleAst& module) {
    std::vector<SemanticToken> tokens;
    collect_semantic_tokens(module, tokens);
    std::sort(tokens.begin(), tokens.end(), [](const SemanticToken& left,
                                               const SemanticToken& right) {
        if (left.line != right.line) {
            return left.line < right.line;
        }
        if (left.column != right.column) {
            return left.column < right.column;
        }
        if (left.length != right.length) {
            return left.length < right.length;
        }
        return left.type < right.type;
    });

    std::ostringstream out;
    out << "{\"data\":[";
    int previous_line = 0;
    int previous_column = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const SemanticToken& token = tokens[i];
        if (i > 0) {
            out << ',';
        }
        const int delta_line = i == 0 ? token.line : token.line - previous_line;
        const int delta_column = delta_line == 0 ? token.column - previous_column : token.column;
        out << delta_line << ',' << delta_column << ',' << token.length << ',' << token.type << ','
            << token.modifiers;
        previous_line = token.line;
        previous_column = token.column;
    }
    out << "]}";
    return out.str();
}

} // namespace dudu
