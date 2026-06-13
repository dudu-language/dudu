#pragma once

#include "dudu/source.hpp"

#include <map>
#include <string>
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

struct FieldDecl {
    std::string name;
    std::string type;
    SourceLocation location;
};

struct ParamDecl {
    std::string name;
    std::string type;
    SourceLocation location;
};

struct RawStmt {
    std::string text;
    std::vector<RawStmt> children;
    SourceLocation location;
};

struct TypeAliasDecl {
    std::string name;
    std::string type;
    SourceLocation location;
};

struct NativeTypeDecl {
    std::string name;
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
    std::vector<EnumValueDecl> values;
    SourceLocation location;
};

struct FunctionDecl {
    Visibility visibility = Visibility::Default;
    std::string name;
    std::vector<Decorator> decorators;
    std::vector<ParamDecl> params;
    std::string return_type;
    std::vector<RawStmt> body;
    SourceLocation location;
};

struct ClassDecl {
    Visibility visibility = Visibility::Default;
    std::string name;
    std::vector<Decorator> decorators;
    std::vector<FieldDecl> fields;
    std::vector<FunctionDecl> methods;
    SourceLocation location;
};

struct ConstDecl {
    std::string name;
    std::string type;
    std::string value;
    SourceLocation location;
};

struct StaticAssertDecl {
    std::string expression;
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

} // namespace dudu
