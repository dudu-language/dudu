#pragma once

#include "dudu/core/source.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {

enum class Visibility {
    Default,
    Private,
};

enum class CompilationDomain {
    Target,
    MacroHost,
};

enum class ImportKind {
    Module,
    From,
    ForeignC,
    ForeignCxx,
    ForeignCpp,
};

enum class NativeIncludeStyle {
    System,
    Path,
};

struct ImportDecl {
    ImportKind kind = ImportKind::Module;
    NativeIncludeStyle native_include_style = NativeIncludeStyle::System;
    std::string module_path;
    std::string imported_name;
    std::string alias;
    SourceLocation location;
    SourceRange range;
    SourceRange module_range;
    SourceRange imported_name_range;
    SourceRange alias_range;
    SourceRange native_language_range;
    SourceRange native_path_mode_range;
};

struct ModuleDependency {
    ImportKind kind = ImportKind::Module;
    std::string import_module_path;
    std::string resolved_module_path;
    std::filesystem::path source_path;
    SourceLocation location;
};

enum class TypeKind {
    Unknown,
    Named,
    Qualified,
    Value,
    Template,
    Associated,
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
    PackExpansion,
    Shaped,
    AssociatedTemplate,
    NativeTransform,
};

enum class ReferenceKind {
    Lvalue,
    Rvalue,
};

struct TypeRef {
    TypeKind kind = TypeKind::Unknown;
    ReferenceKind reference_kind = ReferenceKind::Lvalue;
    bool malformed = false;
    SourceTextAtom name;
    SourceTextAtom value;
    std::vector<TypeRef> children;
    SourceLocation location;
    SourceRange range;
};

struct ParamDecl {
    std::string name;
    TypeRef type_ref;
    SourceLocation location;
    bool variadic = false;
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

enum class UnsupportedFeature {
    None,
    Exceptions,
    Generators,
    Async,
    ContextManagers,
    GlobalRebinding,
    NonlocalRebinding,
    DynamicDeletion,
    LocalFunctionDeclarations,
    LocalImports,
};

enum class ExprKind {
    Missing,
    Unknown,
    Name,
    TypeExpr,
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
    Ellipsis,
    NewAxis,
    PackExpansion,
    SetLiteral,
    TupleLiteral,
    DefExpression,
    Comprehension,
    Lambda,
    Conditional,
    Await,
    Yield,
    CppEscape,
};

enum class ExprOpCode : uint8_t {
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
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    Or,
    Not,
    BitNot,
};

struct ExprOp {
    ExprOpCode code = ExprOpCode::None;

    ExprOp() = default;
    ExprOp(std::string_view text);

    ExprOp& operator=(std::string_view text);
    operator std::string_view() const;
};

std::string_view expr_op_text(ExprOp op);
ExprOp expr_op_from_text(std::string_view text);
bool operator==(ExprOp left, std::string_view right);
bool operator==(std::string_view left, ExprOp right);
bool operator!=(ExprOp left, std::string_view right);
bool operator!=(std::string_view left, ExprOp right);

struct Expr {
    Expr() = default;
    Expr(const Expr& other);
    Expr& operator=(const Expr& other);
    Expr(Expr&&) noexcept = default;
    Expr& operator=(Expr&&) noexcept = default;

    ExprKind kind = ExprKind::Missing;
    SourceTextAtom name;
    SourceTextAtom value;
    ExprOp op;
    SourceLocation op_location;
    std::unique_ptr<std::vector<Expr>> callee;
    std::unique_ptr<std::vector<Expr>> template_args;
    std::unique_ptr<std::vector<TypeRef>> template_type_args;
    std::unique_ptr<TypeRef> type_ref;
    std::vector<Expr> children;
    SourceLocation location;
    SourceRange range;
};

struct Decorator {
    Expr expr;
    SourceLocation location;
};

struct FieldDecl {
    std::string name;
    TypeRef type_ref;
    Expr value_expr;
    std::vector<Decorator> decorators;
    SourceLocation location;
    std::string doc_comment{};
};

struct Stmt {
    StmtKind kind = StmtKind::Unknown;
    std::string name;
    std::vector<std::string> cpp_lines;
    std::shared_ptr<TypeRef> type_ref;
    CompoundAssignOp compound_op = CompoundAssignOp::None;
    UnsupportedFeature unsupported_feature = UnsupportedFeature::None;
    Expr expr;
    Expr value_expr;
    std::shared_ptr<Expr> target_expr;
    std::shared_ptr<Expr> condition_expr;
    std::shared_ptr<Expr> message_expr;
    std::shared_ptr<Expr> iterable_expr;
    std::shared_ptr<Expr> pattern_expr;
    std::shared_ptr<Expr> guard_expr;
    std::vector<Stmt> children;
    SourceLocation location;
    SourceRange range;
};

struct TypeAliasDecl {
    std::string name;
    std::string cpp_name;
    TypeRef type_ref;
    std::vector<std::string> generic_params{};
    std::optional<size_t> generic_min_args{};
    std::vector<TypeRef> generic_default_args{};
    std::string origin_module;
    SourceLocation location;
    std::string doc_comment{};
};

struct NativeSymbolId {
    std::string usr;
    std::string canonical_path;
};

struct TypeLayout {
    size_t size = 0;
    size_t alignment = 1;
};

struct NativeParameterMetadata {
    std::string name;
    std::string default_value;
    std::string doc_comment;
};

struct NativeDeclarationMetadata {
    std::string declaration;
    std::string summary_doc_comment;
    std::string return_doc_comment;
    std::string deprecated_message;
    std::vector<NativeParameterMetadata> parameters;
    std::vector<NativeParameterMetadata> template_parameters;
};

struct NativeTypeDecl {
    std::string name;
    std::string native_spelling;
    TypeRef type_ref;
    bool enum_type = false;
    NativeSymbolId identity{};
    std::optional<TypeLayout> layout{};
    SourceLocation location;
    std::string doc_comment{};
    std::vector<std::string> generic_params{};
    std::optional<size_t> generic_min_args{};
    std::vector<TypeRef> generic_default_args{};
    NativeDeclarationMetadata native_metadata{};
};

struct NativeValueDecl {
    std::string name;
    std::string native_spelling;
    TypeRef type_ref;
    bool enum_constant = false;
    NativeSymbolId identity{};
    SourceLocation location;
    std::string doc_comment{};
    NativeDeclarationMetadata native_metadata{};
};

struct NativeFunctionDecl {
    std::string name;
    std::vector<std::string> template_params;
    std::vector<bool> template_param_is_value;
    std::vector<TypeRef> template_default_args;
    std::vector<std::string> param_names;
    std::vector<std::string> param_native_spellings;
    std::vector<TypeRef> param_type_refs;
    std::string return_native_spelling;
    TypeRef return_type_ref;
    int min_params = -1;
    bool variadic = false;
    bool deleted = false;
    NativeSymbolId identity{};
    SourceLocation location;
    std::string doc_comment{};
    NativeDeclarationMetadata native_metadata{};
};

struct NativeMacroDecl {
    std::string name;
    int arity = -1;
    bool function_like = false;
    std::vector<std::string> param_names{};
    NativeSymbolId identity{};
    SourceLocation location;
    std::string doc_comment{};
};

struct NativeNamespaceDecl {
    std::string name;
    NativeSymbolId identity{};
    SourceLocation location;
    std::string doc_comment{};
};

struct EnumPayloadField {
    std::string name;
    TypeRef type_ref;
    std::vector<Decorator> decorators;
    SourceLocation location;
};

struct EnumValueDecl {
    std::string name;
    Expr value_expr;
    std::vector<EnumPayloadField> payload_fields;
    std::vector<Decorator> decorators;
    SourceLocation location;
    std::string doc_comment{};
};

struct FunctionDecl;

struct GenericParamDecl {
    std::string name;
    bool variadic = false;
    SourceLocation location;
};

struct EnumDecl {
    std::string name;
    std::string cpp_name;
    TypeRef underlying_type_ref;
    std::string origin_module;
    std::vector<EnumValueDecl> values;
    std::vector<FunctionDecl> methods;
    std::vector<Decorator> decorators;
    SourceLocation location;
    SourceRange range;
    std::string doc_comment{};
};

struct FunctionDecl {
    Visibility visibility = Visibility::Default;
    std::string name;
    std::string cpp_name;
    NativeSymbolId native_identity{};
    TypeRef receiver_type_ref;
    std::vector<std::string> generic_params;
    std::vector<GenericParamDecl> generic_param_decls;
    std::vector<bool> generic_param_is_value;
    std::vector<TypeRef> generic_default_args;
    std::vector<Decorator> decorators;
    std::vector<ParamDecl> params;
    TypeRef return_type_ref;
    std::string origin_module;
    std::vector<Stmt> statements;
    bool deleted = false;
    int min_params = -1;
    bool body_syntax_damaged = false;
    SourceLocation location;
    SourceRange range;
    std::string doc_comment{};
    NativeDeclarationMetadata native_metadata{};
};

struct ConstDecl {
    std::string name;
    std::string cpp_name;
    TypeRef type_ref;
    Expr value_expr;
    std::vector<Decorator> decorators;
    std::string origin_module;
    SourceLocation location;
    std::string doc_comment{};
};

struct BaseClassDecl {
    TypeRef type_ref;
    SourceLocation location;
};

struct ClassDecl {
    Visibility visibility = Visibility::Default;
    std::string name;
    std::string cpp_name;
    NativeSymbolId identity{};
    std::optional<TypeLayout> layout{};
    bool native_declaration = false;
    std::vector<std::string> generic_params;
    std::vector<GenericParamDecl> generic_param_decls;
    std::optional<size_t> generic_min_args;
    std::vector<TypeRef> generic_default_args;
    std::vector<TypeRef> native_specialization_args;
    std::vector<TypeRef> native_specialization_requirements;
    bool native_partial_specialization = false;
    std::vector<BaseClassDecl> base_class_refs;
    std::vector<TypeAliasDecl> type_aliases;
    std::vector<Decorator> decorators;
    std::vector<FieldDecl> fields;
    std::vector<ConstDecl> constants;
    std::vector<ConstDecl> static_fields;
    std::vector<FunctionDecl> methods;
    std::vector<EnumDecl> enums;
    std::string origin_module;
    SourceLocation location;
    SourceRange range;
    std::string doc_comment{};
    NativeDeclarationMetadata native_metadata{};
};

struct StaticAssertDecl {
    Expr expression_expr;
    SourceLocation location;
};

enum class GeneratedDeclarationKind {
    Class,
    Enum,
    Function,
    Field,
    Constant,
    Implementation,
};

struct GeneratedDeclarationOrigin {
    GeneratedDeclarationKind kind = GeneratedDeclarationKind::Function;
    std::string module_path;
    std::string owner;
    std::string name;
    std::string macro_name;
    std::string macro_identity;
    SourceRange invocation;
    SourceRange definition;
    SourceRange source_declaration;
};

struct ModuleAst {
    std::filesystem::path source_path;
    std::string module_path;
    std::string source_digest;
    std::string doc_comment{};
    CompilationDomain compilation_domain = CompilationDomain::Target;
    std::map<std::string, std::string> build_values;
    bool target_mode_explicit = false;
    std::vector<ImportDecl> imports;
    std::vector<ModuleDependency> dependencies;
    std::vector<TypeAliasDecl> aliases;
    std::vector<NativeTypeDecl> native_types;
    std::vector<NativeValueDecl> native_values;
    std::vector<NativeFunctionDecl> native_functions;
    std::vector<NativeMacroDecl> native_macros;
    std::vector<NativeNamespaceDecl> native_namespaces;
    std::vector<ClassDecl> native_classes;
    std::vector<EnumDecl> imported_enum_shapes;
    std::vector<std::string> module_strip_prefixes;
    std::vector<std::string> module_import_prefixes;
    std::vector<EnumDecl> enums;
    std::vector<ClassDecl> classes;
    std::vector<FunctionDecl> functions;
    std::vector<ConstDecl> constants;
    std::vector<StaticAssertDecl> static_asserts;
    std::set<std::string> resolved_macro_decorators;
    std::vector<GeneratedDeclarationOrigin> generated_origins;
    std::vector<ModuleAst> module_units;
};

std::string bound_import_name(const ImportDecl& import);
std::string render_import_decl(const ImportDecl& import);
std::string_view statement_kind_name(StmtKind kind);
std::string_view expression_kind_name(ExprKind kind);
std::string_view type_kind_name(TypeKind kind);
Expr parse_expr_text(std::string_view text, SourceLocation location = {});
TypeRef parse_type_text(std::string_view text, SourceLocation location = {});
std::vector<std::string> tuple_binding_names(const Expr& expr);

} // namespace dudu
