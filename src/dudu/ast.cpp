#include "dudu/ast.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace dudu {
Expr::Expr(const Expr& other)
    : kind(other.kind), name(other.name), value(other.value), op(other.op),
      callee(other.callee == nullptr ? nullptr
                                     : std::make_unique<std::vector<Expr>>(*other.callee)),
      template_args(other.template_args == nullptr
                        ? nullptr
                        : std::make_unique<std::vector<Expr>>(*other.template_args)),
      template_type_args(other.template_type_args == nullptr
                             ? nullptr
                             : std::make_unique<std::vector<TypeRef>>(*other.template_type_args)),
      type_ref(other.type_ref == nullptr ? nullptr : std::make_unique<TypeRef>(*other.type_ref)),
      children(other.children), location(other.location), range(other.range) {
}

Expr& Expr::operator=(const Expr& other) {
    if (this == &other) {
        return *this;
    }
    kind = other.kind;
    name = other.name;
    value = other.value;
    op = other.op;
    callee =
        other.callee == nullptr ? nullptr : std::make_unique<std::vector<Expr>>(*other.callee);
    template_args = other.template_args == nullptr
                        ? nullptr
                        : std::make_unique<std::vector<Expr>>(*other.template_args);
    template_type_args = other.template_type_args == nullptr
                             ? nullptr
                             : std::make_unique<std::vector<TypeRef>>(*other.template_type_args);
    type_ref = other.type_ref == nullptr ? nullptr : std::make_unique<TypeRef>(*other.type_ref);
    children = other.children;
    location = other.location;
    range = other.range;
    return *this;
}

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

std::string render_import_decl(const ImportDecl& import) {
    std::ostringstream out;
    if (import.kind == ImportKind::From) {
        out << "from " << import.module_path << " import " << import.imported_name;
    } else {
        out << "import ";
        if (import.kind == ImportKind::ForeignC) {
            out << "c ";
        } else if (import.kind == ImportKind::ForeignCxx) {
            out << "cxx ";
        } else if (import.kind == ImportKind::ForeignCpp) {
            out << "cpp ";
        }
        out << import.module_path;
    }
    if (!import.alias.empty()) {
        out << " as " << import.alias;
    }
    return out.str();
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
    case StmtKind::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

std::string_view expression_kind_name(ExprKind kind) {
    switch (kind) {
    case ExprKind::Missing:
        return "missing";
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
    case ExprKind::DefExpression:
        return "def_expression";
    case ExprKind::Comprehension:
        return "comprehension";
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
    case TypeKind::PackExpansion:
        return "pack_expansion";
    }
    return "unknown";
}

} // namespace dudu
