#pragma once

#include "dudu/source.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

enum class Visibility {
    Default,
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
    std::string source_text;
    SourceLocation location;
    SourceRange range;
    SourceRange module_range;
};

enum class TypeKind {
    Unknown,
    Named,
    Qualified,
    Value,
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
    Match,
    Case,
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
    Unsupported,
};

enum class CompoundAssignOp {
    None,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    BitAnd,
    BitOr,
    BitXor,
    ShiftLeft,
    ShiftRight,
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
    Slice,
    SetLiteral,
    TupleLiteral,
    Lambda,
    Conditional,
    Await,
    Yield,
    CppEscape,
};

struct Expr {
    ExprKind kind = ExprKind::Unknown;
    std::string text;
    std::string name;
    std::string value;
    std::string op;
    std::vector<Expr> callee;
    std::vector<Expr> params;
    std::vector<Expr> template_args;
    std::vector<TypeRef> template_type_args;
    std::vector<Expr> children;
    SourceLocation location;
    SourceRange range;
};

struct Decorator {
    std::string text;
    Expr expr;
    SourceLocation location;
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
    std::string source_text;
    std::string name;
    std::string cpp_body;
    TypeRef type_ref;
    CompoundAssignOp compound_op = CompoundAssignOp::None;
    std::string unsupported_feature;
    Expr expr;
    Expr value_expr;
    Expr target_expr;
    Expr condition_expr;
    Expr message_expr;
    Expr iterable_expr;
    Expr pattern_expr;
    Expr guard_expr;
    std::vector<Stmt> children;
    SourceLocation location;
    SourceRange range;
};

struct TypeAliasDecl {
    std::string name;
    std::string cpp_name;
    std::string type;
    TypeRef type_ref;
    std::string origin_module;
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
    bool enum_constant = false;
    SourceLocation location;
};

struct NativeFunctionDecl {
    std::string name;
    std::vector<std::string> template_params;
    std::vector<std::string> params;
    std::string return_type;
    int min_params = -1;
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

struct EnumPayloadField {
    std::string name;
    std::string type;
    TypeRef type_ref;
    SourceLocation location;
};

struct EnumValueDecl {
    std::string name;
    std::string value;
    Expr value_expr;
    std::vector<EnumPayloadField> payload_fields;
    bool tuple_payload = false;
    SourceLocation location;
};

struct EnumDecl {
    std::string name;
    std::string cpp_name;
    std::string underlying_type;
    TypeRef underlying_type_ref;
    std::string origin_module;
    std::vector<EnumValueDecl> values;
    SourceLocation location;
};

struct FunctionDecl {
    Visibility visibility = Visibility::Default;
    std::string name;
    std::string cpp_name;
    std::string receiver_type;
    std::vector<std::string> generic_params;
    std::vector<Decorator> decorators;
    std::vector<ParamDecl> params;
    std::string return_type;
    TypeRef return_type_ref;
    std::string origin_module;
    std::vector<Stmt> statements;
    SourceLocation location;
};

struct ConstDecl {
    std::string name;
    std::string cpp_name;
    std::string type;
    std::string value;
    TypeRef type_ref;
    Expr value_expr;
    std::string origin_module;
    SourceLocation location;
};

struct BaseClassDecl {
    std::string type;
    TypeRef type_ref;
    SourceLocation location;
};

struct ClassDecl {
    Visibility visibility = Visibility::Default;
    std::string name;
    std::string cpp_name;
    std::vector<std::string> generic_params;
    std::vector<std::string> base_classes;
    std::vector<BaseClassDecl> base_class_refs;
    std::vector<Decorator> decorators;
    std::vector<FieldDecl> fields;
    std::vector<ConstDecl> constants;
    std::vector<ConstDecl> static_fields;
    std::vector<FunctionDecl> methods;
    std::string origin_module;
    SourceLocation location;
};

struct StaticAssertDecl {
    std::string expression;
    Expr expression_expr;
    SourceLocation location;
};

struct ModuleAst {
    std::filesystem::path source_path;
    std::string module_path;
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
    std::vector<std::string> module_strip_prefixes;
    std::vector<EnumDecl> enums;
    std::vector<ClassDecl> classes;
    std::vector<FunctionDecl> functions;
    std::vector<ConstDecl> constants;
    std::vector<StaticAssertDecl> static_asserts;
    std::vector<ModuleAst> module_units;
};

std::string bound_import_name(const ImportDecl& import);
std::string_view statement_kind_name(StmtKind kind);
std::string_view expression_kind_name(ExprKind kind);
std::string_view type_kind_name(TypeKind kind);
Expr parse_expr_text(std::string_view text, SourceLocation location = {});
TypeRef parse_type_text(std::string_view text, SourceLocation location = {});
std::vector<std::string> tuple_binding_names(const Expr& expr);

} // namespace dudu
