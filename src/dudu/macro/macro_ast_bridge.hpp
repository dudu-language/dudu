#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/macro/macro_protocol_generated.hpp"
#include "dudu/macro/macro_registry.hpp"

namespace dudu::macro {

protocol::SourceRange to_protocol(const SourceRange& range);
protocol::SourceRange to_protocol(SourceLocation location);
protocol::TypeRef to_protocol(const TypeRef& type);
protocol::Expression to_protocol(const Expr& expression);
protocol::Statement to_protocol(const Stmt& statement);
protocol::Attribute to_protocol(const Decorator& decorator);
protocol::FieldDecl to_protocol(const FieldDecl& field, const std::string& module_path,
                                const std::string& owner = {});
protocol::FunctionDecl to_protocol(const FunctionDecl& function, const std::string& module_path,
                                   const std::string& owner = {});
protocol::EnumDecl to_protocol(const EnumDecl& value, const std::string& module_path);
protocol::ClassDecl to_protocol(const ClassDecl& value, const std::string& module_path);
protocol::ConstantDecl to_protocol(const ConstDecl& value, const std::string& module_path,
                                   const std::string& owner = {});
protocol::Declaration declaration_for_invocation(const ModuleAst& module,
                                                 const Invocation& invocation);
std::vector<protocol::Declaration>
declarations_for_invocations(const ModuleAst& module, const std::vector<Invocation>& invocations);

SourceRange from_protocol(const protocol::SourceRange& range, SourceLocation fallback = {});
TypeRef from_protocol(const protocol::TypeRef& type, SourceLocation fallback = {});
Expr from_protocol(const protocol::Expression& expression, SourceLocation fallback = {});
Stmt from_protocol(const protocol::Statement& statement, SourceLocation fallback = {});
Decorator from_protocol(const protocol::Attribute& attribute, SourceLocation fallback = {});
FieldDecl from_protocol(const protocol::FieldDecl& field, SourceLocation fallback = {});
FunctionDecl from_protocol(const protocol::FunctionDecl& function, const std::string& module_path,
                           const std::string& owner = {}, SourceLocation fallback = {});
EnumDecl from_protocol(const protocol::EnumDecl& value, const std::string& module_path,
                       SourceLocation fallback = {});
ClassDecl from_protocol(const protocol::ClassDecl& value, const std::string& module_path,
                        SourceLocation fallback = {});
ConstDecl from_protocol(const protocol::ConstantDecl& value, const std::string& module_path,
                        const std::string& owner = {}, SourceLocation fallback = {});

} // namespace dudu::macro
