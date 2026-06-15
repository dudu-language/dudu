#include "dudu/ast.hpp"

#include <string>
#include <vector>

namespace dudu {
std::vector<std::string> tuple_binding_names(const Expr& expr) {
    if (expr.kind != ExprKind::TupleLiteral) {
        return {};
    }
    std::vector<std::string> names;
    names.reserve(expr.children.size());
    for (const Expr& child : expr.children) {
        if (child.kind != ExprKind::Name || child.name.empty()) {
            return {};
        }
        names.push_back(child.name);
    }
    return names;
}

std::string bound_import_name(const ImportDecl& import) {
    if (!import.alias.empty()) {
        return import.alias;
    }
    if (import.kind == ImportKind::From) {
        return import.imported_name;
    }
    const size_t dot = import.module_path.find('.');
    if (dot == std::string::npos) {
        return import.module_path;
    }
    return import.module_path.substr(0, dot);
}

std::string_view statement_kind_name(StmtKind kind) {
    switch (kind) {
    case StmtKind::Unknown:
        return "unknown";
    case StmtKind::Expr:
        return "expr";
    case StmtKind::VarDecl:
        return "var_decl";
    case StmtKind::Assign:
        return "assign";
    case StmtKind::CompoundAssign:
        return "compound_assign";
    case StmtKind::Return:
        return "return";
    case StmtKind::If:
        return "if";
    case StmtKind::Elif:
        return "elif";
    case StmtKind::Else:
        return "else";
    case StmtKind::Match:
        return "match";
    case StmtKind::Case:
        return "case";
    case StmtKind::While:
        return "while";
    case StmtKind::For:
        return "for";
    case StmtKind::Break:
        return "break";
    case StmtKind::Continue:
        return "continue";
    case StmtKind::Try:
        return "try";
    case StmtKind::Except:
        return "except";
    case StmtKind::Raise:
        return "raise";
    case StmtKind::Delete:
        return "delete";
    case StmtKind::Assert:
        return "assert";
    case StmtKind::DebugAssert:
        return "debug_assert";
    case StmtKind::CppEscape:
        return "cpp_escape";
    case StmtKind::Pass:
        return "pass";
    }
    return "unknown";
}

std::string_view expression_kind_name(ExprKind kind) {
    switch (kind) {
    case ExprKind::Unknown:
        return "unknown";
    case ExprKind::Name:
        return "name";
    case ExprKind::BoolLiteral:
        return "bool_literal";
    case ExprKind::IntLiteral:
        return "int_literal";
    case ExprKind::FloatLiteral:
        return "float_literal";
    case ExprKind::StringLiteral:
        return "string_literal";
    case ExprKind::NoneLiteral:
        return "none_literal";
    case ExprKind::Unary:
        return "unary";
    case ExprKind::Binary:
        return "binary";
    case ExprKind::Call:
        return "call";
    case ExprKind::TemplateCall:
        return "template_call";
    case ExprKind::Member:
        return "member";
    case ExprKind::Index:
        return "index";
    case ExprKind::ListLiteral:
        return "list_literal";
    case ExprKind::DictLiteral:
        return "dict_literal";
    case ExprKind::DictEntry:
        return "dict_entry";
    case ExprKind::NamedArg:
        return "named_arg";
    case ExprKind::Slice:
        return "slice";
    case ExprKind::SetLiteral:
        return "set_literal";
    case ExprKind::TupleLiteral:
        return "tuple_literal";
    case ExprKind::Lambda:
        return "lambda";
    case ExprKind::Conditional:
        return "conditional";
    case ExprKind::Await:
        return "await";
    case ExprKind::Yield:
        return "yield";
    case ExprKind::CppEscape:
        return "cpp_escape";
    }
    return "unknown";
}

std::string_view type_kind_name(TypeKind kind) {
    switch (kind) {
    case TypeKind::Unknown:
        return "unknown";
    case TypeKind::Named:
        return "named";
    case TypeKind::Qualified:
        return "qualified";
    case TypeKind::Value:
        return "value";
    case TypeKind::Template:
        return "template";
    case TypeKind::Pointer:
        return "pointer";
    case TypeKind::Reference:
        return "reference";
    case TypeKind::Const:
        return "const";
    case TypeKind::Volatile:
        return "volatile";
    case TypeKind::Atomic:
        return "atomic";
    case TypeKind::Device:
        return "device";
    case TypeKind::Storage:
        return "storage";
    case TypeKind::Shared:
        return "shared";
    case TypeKind::Static:
        return "static";
    case TypeKind::FixedArray:
        return "fixed_array";
    case TypeKind::Function:
        return "function";
    }
    return "unknown";
}

} // namespace dudu
