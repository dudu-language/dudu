#include "dudu/macro/macro_ast_bridge.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace dudu::macro {
namespace {

namespace p = protocol;

p::ExpressionKind expression_kind(ExprKind kind) {
    switch (kind) {
    case ExprKind::Missing: return p::ExpressionKind::Missing;
    case ExprKind::Name: return p::ExpressionKind::Name;
    case ExprKind::BoolLiteral: return p::ExpressionKind::BoolLiteral;
    case ExprKind::IntLiteral: return p::ExpressionKind::IntLiteral;
    case ExprKind::FloatLiteral: return p::ExpressionKind::FloatLiteral;
    case ExprKind::StringLiteral: return p::ExpressionKind::StringLiteral;
    case ExprKind::NoneLiteral: return p::ExpressionKind::NoneLiteral;
    case ExprKind::Unary: return p::ExpressionKind::Unary;
    case ExprKind::Binary: return p::ExpressionKind::Binary;
    case ExprKind::Call: return p::ExpressionKind::Call;
    case ExprKind::TemplateCall: return p::ExpressionKind::TemplateCall;
    case ExprKind::Member: return p::ExpressionKind::Member;
    case ExprKind::Index: return p::ExpressionKind::Index;
    case ExprKind::ListLiteral: return p::ExpressionKind::ListLiteral;
    case ExprKind::DictLiteral: return p::ExpressionKind::DictLiteral;
    case ExprKind::DictEntry: return p::ExpressionKind::DictEntry;
    case ExprKind::NamedArg: return p::ExpressionKind::NamedArg;
    case ExprKind::Slice: return p::ExpressionKind::Slice;
    case ExprKind::Ellipsis: return p::ExpressionKind::Ellipsis;
    case ExprKind::NewAxis: return p::ExpressionKind::NewAxis;
    case ExprKind::PackExpansion: return p::ExpressionKind::PackExpansion;
    case ExprKind::SetLiteral: return p::ExpressionKind::SetLiteral;
    case ExprKind::TupleLiteral: return p::ExpressionKind::TupleLiteral;
    case ExprKind::CppEscape: return p::ExpressionKind::CppEscape;
    case ExprKind::Unknown:
    case ExprKind::DefExpression:
    case ExprKind::Comprehension:
    case ExprKind::Lambda:
    case ExprKind::Conditional:
    case ExprKind::Await:
    case ExprKind::Yield:
        throw std::runtime_error("expression is not part of the public macro AST");
    }
    throw std::runtime_error("unknown compiler expression kind");
}

ExprKind expression_kind(p::ExpressionKind kind) {
    switch (kind) {
    case p::ExpressionKind::Missing: return ExprKind::Missing;
    case p::ExpressionKind::Name: return ExprKind::Name;
    case p::ExpressionKind::BoolLiteral: return ExprKind::BoolLiteral;
    case p::ExpressionKind::IntLiteral: return ExprKind::IntLiteral;
    case p::ExpressionKind::FloatLiteral: return ExprKind::FloatLiteral;
    case p::ExpressionKind::StringLiteral: return ExprKind::StringLiteral;
    case p::ExpressionKind::NoneLiteral: return ExprKind::NoneLiteral;
    case p::ExpressionKind::Unary: return ExprKind::Unary;
    case p::ExpressionKind::Binary: return ExprKind::Binary;
    case p::ExpressionKind::Call: return ExprKind::Call;
    case p::ExpressionKind::TemplateCall: return ExprKind::TemplateCall;
    case p::ExpressionKind::Member: return ExprKind::Member;
    case p::ExpressionKind::Index: return ExprKind::Index;
    case p::ExpressionKind::ListLiteral: return ExprKind::ListLiteral;
    case p::ExpressionKind::DictLiteral: return ExprKind::DictLiteral;
    case p::ExpressionKind::DictEntry: return ExprKind::DictEntry;
    case p::ExpressionKind::NamedArg: return ExprKind::NamedArg;
    case p::ExpressionKind::Slice: return ExprKind::Slice;
    case p::ExpressionKind::Ellipsis: return ExprKind::Ellipsis;
    case p::ExpressionKind::NewAxis: return ExprKind::NewAxis;
    case p::ExpressionKind::PackExpansion: return ExprKind::PackExpansion;
    case p::ExpressionKind::SetLiteral: return ExprKind::SetLiteral;
    case p::ExpressionKind::TupleLiteral: return ExprKind::TupleLiteral;
    case p::ExpressionKind::CppEscape: return ExprKind::CppEscape;
    }
    throw std::runtime_error("unknown public macro expression kind");
}

p::StatementKind statement_kind(StmtKind kind) {
    switch (kind) {
    case StmtKind::Unknown: return p::StatementKind::Unknown;
    case StmtKind::Expr: return p::StatementKind::Expression;
    case StmtKind::VarDecl: return p::StatementKind::Variable;
    case StmtKind::Assign: return p::StatementKind::Assign;
    case StmtKind::CompoundAssign: return p::StatementKind::CompoundAssign;
    case StmtKind::Return: return p::StatementKind::Return;
    case StmtKind::If: return p::StatementKind::If;
    case StmtKind::Elif: return p::StatementKind::Elif;
    case StmtKind::Else: return p::StatementKind::Else;
    case StmtKind::Match: return p::StatementKind::Match;
    case StmtKind::Case: return p::StatementKind::Case;
    case StmtKind::While: return p::StatementKind::While;
    case StmtKind::For: return p::StatementKind::For;
    case StmtKind::Break: return p::StatementKind::Break;
    case StmtKind::Continue: return p::StatementKind::Continue;
    case StmtKind::Try: return p::StatementKind::Try;
    case StmtKind::Except: return p::StatementKind::Except;
    case StmtKind::Raise: return p::StatementKind::Raise;
    case StmtKind::Delete: return p::StatementKind::Delete;
    case StmtKind::Assert: return p::StatementKind::Assert;
    case StmtKind::DebugAssert: return p::StatementKind::DebugAssert;
    case StmtKind::CppEscape: return p::StatementKind::CppEscape;
    case StmtKind::Pass: return p::StatementKind::Pass;
    case StmtKind::Unsupported:
        throw std::runtime_error("unsupported statement cannot enter the public macro AST");
    }
    throw std::runtime_error("unknown compiler statement kind");
}

StmtKind statement_kind(p::StatementKind kind) {
    switch (kind) {
    case p::StatementKind::Unknown: return StmtKind::Unknown;
    case p::StatementKind::Expression: return StmtKind::Expr;
    case p::StatementKind::Variable: return StmtKind::VarDecl;
    case p::StatementKind::Assign: return StmtKind::Assign;
    case p::StatementKind::CompoundAssign: return StmtKind::CompoundAssign;
    case p::StatementKind::Return: return StmtKind::Return;
    case p::StatementKind::If: return StmtKind::If;
    case p::StatementKind::Elif: return StmtKind::Elif;
    case p::StatementKind::Else: return StmtKind::Else;
    case p::StatementKind::Match: return StmtKind::Match;
    case p::StatementKind::Case: return StmtKind::Case;
    case p::StatementKind::While: return StmtKind::While;
    case p::StatementKind::For: return StmtKind::For;
    case p::StatementKind::Break: return StmtKind::Break;
    case p::StatementKind::Continue: return StmtKind::Continue;
    case p::StatementKind::Try: return StmtKind::Try;
    case p::StatementKind::Except: return StmtKind::Except;
    case p::StatementKind::Raise: return StmtKind::Raise;
    case p::StatementKind::Delete: return StmtKind::Delete;
    case p::StatementKind::Assert: return StmtKind::Assert;
    case p::StatementKind::DebugAssert: return StmtKind::DebugAssert;
    case p::StatementKind::CppEscape: return StmtKind::CppEscape;
    case p::StatementKind::Pass: return StmtKind::Pass;
    }
    throw std::runtime_error("unknown public macro statement kind");
}

std::string compound_operator(CompoundAssignOp op) {
    switch (op) {
    case CompoundAssignOp::None: return {};
    case CompoundAssignOp::Add: return "+=";
    case CompoundAssignOp::Sub: return "-=";
    case CompoundAssignOp::Mul: return "*=";
    case CompoundAssignOp::Div: return "/=";
    case CompoundAssignOp::Mod: return "%=";
    case CompoundAssignOp::BitAnd: return "&=";
    case CompoundAssignOp::BitOr: return "|=";
    case CompoundAssignOp::BitXor: return "^=";
    case CompoundAssignOp::ShiftLeft: return "<<=";
    case CompoundAssignOp::ShiftRight: return ">>=";
    }
    return {};
}

CompoundAssignOp compound_operator(const std::string& op) {
    if (op == "+=") return CompoundAssignOp::Add;
    if (op == "-=") return CompoundAssignOp::Sub;
    if (op == "*=") return CompoundAssignOp::Mul;
    if (op == "/=") return CompoundAssignOp::Div;
    if (op == "%=") return CompoundAssignOp::Mod;
    if (op == "&=") return CompoundAssignOp::BitAnd;
    if (op == "|=") return CompoundAssignOp::BitOr;
    if (op == "^=") return CompoundAssignOp::BitXor;
    if (op == "<<=") return CompoundAssignOp::ShiftLeft;
    if (op == ">>=") return CompoundAssignOp::ShiftRight;
    if (op.empty()) return CompoundAssignOp::None;
    throw std::runtime_error("unknown macro compound assignment operator: " + op);
}

SourceLocation location_from(const p::SourceRange& range, SourceLocation fallback) {
    SourceLocation out = fallback;
    if (!range.file.empty()) out.file = SourceFileName(range.file);
    if (range.start.line != 0) out.line = static_cast<int>(range.start.line);
    if (range.start.column != 0) out.column = static_cast<int>(range.start.column);
    return out;
}

} // namespace

p::SourceRange to_protocol(const SourceRange& range) {
    return {.file = range.start.file.str(),
            .start = {.line = static_cast<std::uint32_t>(std::max(0, range.start.line)),
                      .column = static_cast<std::uint32_t>(std::max(0, range.start.column)),
                      .offset = 0},
            .end = {.line = static_cast<std::uint32_t>(std::max(0, range.end.line)),
                    .column = static_cast<std::uint32_t>(std::max(0, range.end.column)),
                    .offset = 0}};
}

p::SourceRange to_protocol(SourceLocation location) {
    return to_protocol(SourceRange{.start = location, .end = location});
}

SourceRange from_protocol(const p::SourceRange& range, SourceLocation fallback) {
    SourceRange out;
    out.start = location_from(range, fallback);
    out.end = {range.end.line == 0 ? out.start.line : static_cast<int>(range.end.line),
               range.end.column == 0 ? out.start.column : static_cast<int>(range.end.column)};
    return out;
}

p::TypeRef to_protocol(const TypeRef& type) {
    p::TypeRef out{.kind = static_cast<p::TypeKind>(type.kind),
                   .name = type.name.str(),
                   .value = type.value.str(),
                   .children = {},
                   .range = to_protocol(type.range),
                   .identity = {}};
    for (const TypeRef& child : type.children) out.children.push_back(to_protocol(child));
    return out;
}

TypeRef from_protocol(const p::TypeRef& type, SourceLocation fallback) {
    TypeRef out;
    out.kind = static_cast<TypeKind>(type.kind);
    out.name = type.name;
    out.value = type.value;
    out.range = from_protocol(type.range, fallback);
    out.location = out.range.start;
    for (const p::TypeRef& child : type.children) {
        out.children.push_back(from_protocol(child, out.location));
    }
    return out;
}

p::Expression to_protocol(const Expr& expression) {
    p::Expression out{.kind = expression_kind(expression.kind),
                      .name = expression.name.str(),
                      .value = expression.value.str(),
                      .operator_name = std::string(std::string_view(expression.op)),
                      .children = {},
                      .type_arguments = {},
                      .range = to_protocol(expression.range),
                      .resolved_type = {},
                      .identity = {},
                      .callee = {},
                      .template_arguments = {}};
    for (const Expr& child : expression.children) out.children.push_back(to_protocol(child));
    for (const TypeRef& type : expr_template_type_args(expression)) {
        out.type_arguments.push_back(to_protocol(type));
    }
    if (expression.type_ref != nullptr) out.resolved_type = to_protocol(*expression.type_ref);
    for (const Expr& child : expr_callee(expression)) out.callee.push_back(to_protocol(child));
    for (const Expr& child : expr_template_args(expression)) {
        out.template_arguments.push_back(to_protocol(child));
    }
    return out;
}

Expr from_protocol(const p::Expression& expression, SourceLocation fallback) {
    Expr out;
    out.kind = expression_kind(expression.kind);
    out.name = expression.name;
    out.value = expression.value;
    out.op = expression.operator_name;
    out.range = from_protocol(expression.range, fallback);
    out.location = out.range.start;
    for (const p::Expression& child : expression.children) {
        out.children.push_back(from_protocol(child, out.location));
    }
    std::vector<Expr> callee;
    for (const p::Expression& child : expression.callee) {
        callee.push_back(from_protocol(child, out.location));
    }
    set_expr_callee(out, std::move(callee));
    std::vector<Expr> template_arguments;
    for (const p::Expression& child : expression.template_arguments) {
        template_arguments.push_back(from_protocol(child, out.location));
    }
    set_expr_template_args(out, std::move(template_arguments));
    std::vector<TypeRef> type_arguments;
    for (const p::TypeRef& type : expression.type_arguments) {
        type_arguments.push_back(from_protocol(type, out.location));
    }
    set_expr_template_type_args(out, std::move(type_arguments));
    if (expression.resolved_type.has_value()) {
        out.type_ref =
            std::make_unique<TypeRef>(from_protocol(*expression.resolved_type, out.location));
    }
    return out;
}

p::Statement to_protocol(const Stmt& statement) {
    p::Statement out{.kind = statement_kind(statement.kind),
                     .name = statement.name,
                     .type = {},
                     .expression = {},
                     .value = {},
                     .target = {},
                     .condition = {},
                     .children = {},
                     .range = to_protocol(statement.range),
                     .message = {},
                     .iterable = {},
                     .pattern = {},
                     .guard = {},
                     .operator_name = compound_operator(statement.compound_op)};
    if (statement.type_ref) out.type = to_protocol(*statement.type_ref);
    if (expr_present(statement.expr)) out.expression = to_protocol(statement.expr);
    if (expr_present(statement.value_expr)) out.value = to_protocol(statement.value_expr);
    if (statement.target_expr) out.target = to_protocol(*statement.target_expr);
    if (statement.condition_expr) out.condition = to_protocol(*statement.condition_expr);
    if (statement.message_expr) out.message = to_protocol(*statement.message_expr);
    if (statement.iterable_expr) out.iterable = to_protocol(*statement.iterable_expr);
    if (statement.pattern_expr) out.pattern = to_protocol(*statement.pattern_expr);
    if (statement.guard_expr) out.guard = to_protocol(*statement.guard_expr);
    for (const Stmt& child : statement.children) out.children.push_back(to_protocol(child));
    return out;
}

Stmt from_protocol(const p::Statement& statement, SourceLocation fallback) {
    Stmt out;
    out.kind = statement_kind(statement.kind);
    out.name = statement.name;
    out.compound_op = compound_operator(statement.operator_name);
    out.range = from_protocol(statement.range, fallback);
    out.location = out.range.start;
    if (statement.type) out.type_ref = std::make_shared<TypeRef>(from_protocol(*statement.type, out.location));
    if (statement.expression) out.expr = from_protocol(*statement.expression, out.location);
    if (statement.value) out.value_expr = from_protocol(*statement.value, out.location);
    if (statement.target) out.target_expr = std::make_shared<Expr>(from_protocol(*statement.target, out.location));
    if (statement.condition) out.condition_expr = std::make_shared<Expr>(from_protocol(*statement.condition, out.location));
    if (statement.message) out.message_expr = std::make_shared<Expr>(from_protocol(*statement.message, out.location));
    if (statement.iterable) out.iterable_expr = std::make_shared<Expr>(from_protocol(*statement.iterable, out.location));
    if (statement.pattern) out.pattern_expr = std::make_shared<Expr>(from_protocol(*statement.pattern, out.location));
    if (statement.guard) out.guard_expr = std::make_shared<Expr>(from_protocol(*statement.guard, out.location));
    for (const p::Statement& child : statement.children) out.children.push_back(from_protocol(child, out.location));
    return out;
}

p::Attribute to_protocol(const Decorator& decorator) {
    p::Attribute out{.name = decorator_name(decorator),
                     .arguments = {},
                     .range = to_protocol(decorator.expr.range),
                     .identity = {}};
    if (decorator.expr.kind == ExprKind::Call) {
        for (const Expr& argument : decorator.expr.children) {
            p::AttributeArgument item{.name = {},
                                      .value = {},
                                      .range = to_protocol(argument.range)};
            if (argument.kind == ExprKind::NamedArg && argument.children.size() == 1) {
                item.name = argument.name.str();
                item.value = to_protocol(argument.children.front());
            } else {
                item.value = to_protocol(argument);
            }
            out.arguments.push_back(std::move(item));
        }
    }
    return out;
}

Decorator from_protocol(const p::Attribute& attribute, SourceLocation fallback) {
    Decorator out;
    out.location = from_protocol(attribute.range, fallback).start;
    Expr name;
    name.kind = ExprKind::Name;
    name.name = attribute.name;
    name.location = out.location;
    name.range = from_protocol(attribute.range, out.location);
    if (attribute.arguments.empty()) {
        out.expr = std::move(name);
        return out;
    }
    out.expr.kind = ExprKind::Call;
    out.expr.location = out.location;
    out.expr.range = name.range;
    set_expr_callee(out.expr, {std::move(name)});
    for (const p::AttributeArgument& argument : attribute.arguments) {
        Expr value = from_protocol(argument.value, out.location);
        if (argument.name.empty()) {
            out.expr.children.push_back(std::move(value));
            continue;
        }
        Expr named;
        named.kind = ExprKind::NamedArg;
        named.name = argument.name;
        named.location = from_protocol(argument.range, out.location).start;
        named.range = from_protocol(argument.range, out.location);
        named.children.push_back(std::move(value));
        out.expr.children.push_back(std::move(named));
    }
    return out;
}

} // namespace dudu::macro
