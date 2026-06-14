#pragma once

#include "dudu/source.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

enum class Visibility {
    Default,
    Public,
    Private,
};

enum class ImportKind {
    Module,
    From,
    ForeignC,
    ForeignCpp,
};

struct ImportDecl {
    ImportKind kind = ImportKind::Module;
    std::string module_path;
    std::string imported_name;
    std::string alias;
    SourceLocation location;
};

struct Decorator {
    std::string text;
    SourceLocation location;
};

enum class TypeKind {
    Unknown,
    Named,
    Qualified,
    Template,
    Pointer,
    Reference,
    Const,
    Volatile,
    Atomic,
    Device,
    Storage,
    Shared,
    Static,
    FixedArray,
    Function,
};

struct TypeRef {
    TypeKind kind = TypeKind::Unknown;
    std::string text;
    std::string name;
    std::string value;
    std::vector<TypeRef> children;
    SourceLocation location;
    SourceRange range;
};

struct ParamDecl {
    std::string name;
    std::string type;
    TypeRef type_ref;
    SourceLocation location;
};

enum class StmtKind {
    Unknown,
    Expr,
    VarDecl,
    Assign,
    CompoundAssign,
    Return,
    If,
    Elif,
    Else,
    While,
    For,
    Break,
    Continue,
    Try,
    Except,
    Raise,
    Delete,
    Assert,
    DebugAssert,
    CppEscape,
    Pass,
};

enum class ExprKind {
    Unknown,
    Name,
    BoolLiteral,
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    NoneLiteral,
    Unary,
    Binary,
    Call,
    TemplateCall,
    Member,
    Index,
    ListLiteral,
    DictLiteral,
    DictEntry,
    NamedArg,
    SetLiteral,
    TupleLiteral,
    Lambda,
    Conditional,
    CppEscape,
};

struct Expr {
    ExprKind kind = ExprKind::Unknown;
    std::string text;
    std::string name;
    std::string value;
    std::string op;
    std::vector<Expr> template_args;
    std::vector<Expr> children;
    SourceLocation location;
    SourceRange range;
};

struct FieldDecl {
    std::string name;
    std::string type;
    std::string value;
    TypeRef type_ref;
    Expr value_expr;
    SourceLocation location;
};

struct Stmt {
    StmtKind kind = StmtKind::Unknown;
    std::string text;
    std::string name;
    std::string type;
    std::string value;
    TypeRef type_ref;
    std::string target;
    std::string op;
    std::string condition;
    std::string message;
    std::string iterable;
    Expr expr;
    Expr value_expr;
    Expr target_expr;
    Expr condition_expr;
    Expr message_expr;
    Expr iterable_expr;
    std::vector<Stmt> children;
    SourceLocation location;
    SourceRange range;
};

struct TypeAliasDecl {
    std::string name;
    std::string type;
    TypeRef type_ref;
    SourceLocation location;
};

struct NativeTypeDecl {
    std::string name;
    std::string type;
    SourceLocation location;
};

struct NativeValueDecl {
    std::string name;
    std::string type;
    SourceLocation location;
};

struct NativeFunctionDecl {
    std::string name;
    std::vector<std::string> params;
    std::string return_type;
    bool variadic = false;
    SourceLocation location;
};

struct NativeMacroDecl {
    std::string name;
    int arity = -1;
    bool function_like = false;
    SourceLocation location;
};

struct NativeNamespaceDecl {
    std::string name;
    SourceLocation location;
};

struct EnumValueDecl {
    std::string name;
    std::string value;
    SourceLocation location;
};

struct EnumDecl {
    std::string name;
    std::string underlying_type;
    TypeRef underlying_type_ref;
    std::vector<EnumValueDecl> values;
    SourceLocation location;
};

struct FunctionDecl {
    Visibility visibility = Visibility::Default;
    std::string name;
    std::vector<Decorator> decorators;
    std::vector<ParamDecl> params;
    std::string return_type;
    TypeRef return_type_ref;
    std::vector<Stmt> statements;
    SourceLocation location;
};

struct ConstDecl {
    std::string name;
    std::string type;
    std::string value;
    TypeRef type_ref;
    Expr value_expr;
    SourceLocation location;
};

struct ClassDecl {
    Visibility visibility = Visibility::Default;
    std::string name;
    std::vector<std::string> base_classes;
    std::vector<Decorator> decorators;
    std::vector<FieldDecl> fields;
    std::vector<ConstDecl> constants;
    std::vector<ConstDecl> static_fields;
    std::vector<FunctionDecl> methods;
    SourceLocation location;
};

struct StaticAssertDecl {
    std::string expression;
    Expr expression_expr;
    SourceLocation location;
};

struct ModuleAst {
    std::map<std::string, std::string> build_values;
    bool target_mode_explicit = false;
    std::vector<ImportDecl> imports;
    std::vector<TypeAliasDecl> aliases;
    std::vector<NativeTypeDecl> native_types;
    std::vector<NativeValueDecl> native_values;
    std::vector<NativeFunctionDecl> native_functions;
    std::vector<NativeMacroDecl> native_macros;
    std::vector<NativeNamespaceDecl> native_namespaces;
    std::vector<ClassDecl> native_classes;
    std::vector<EnumDecl> enums;
    std::vector<ClassDecl> classes;
    std::vector<FunctionDecl> functions;
    std::vector<ConstDecl> constants;
    std::vector<StaticAssertDecl> static_asserts;
};

std::string bound_import_name(const ImportDecl& import);
std::string_view statement_kind_name(StmtKind kind);
std::string_view expression_kind_name(ExprKind kind);
std::string_view type_kind_name(TypeKind kind);
StmtKind classify_statement_text(std::string_view text);
Expr parse_expr_text(std::string_view text, SourceLocation location = {});
TypeRef parse_type_text(std::string_view text, SourceLocation location = {});
std::vector<std::string> tuple_binding_names(const Expr& expr);
Stmt statement_from_text(std::string text, SourceLocation location, SourceRange range,
                         std::vector<Stmt> children = {});

} // namespace dudu
