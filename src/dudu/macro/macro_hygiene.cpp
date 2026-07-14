#include "dudu/macro/macro_hygiene.hpp"

#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace dudu::macro {
namespace {

namespace p = protocol;

using Renames = std::map<std::string, std::string>;

struct Scope {
    std::set<std::string> values;
    std::set<std::string> types;
};

std::string stable_suffix(const std::string& value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string private_name(const std::string& original, const std::string& seed) {
    return "__dudu_macro_" + stable_suffix(seed + ":" + original) + "_" + original;
}

void rename_type(p::TypeRef& type, const Renames& renames, const Scope& scope) {
    if (const auto found = renames.find(type.name);
        found != renames.end() && !scope.types.contains(type.name))
        type.name = found->second;
    for (p::TypeRef& child : type.children)
        rename_type(child, renames, scope);
}

void rename_expression(p::Expression& expression, const Renames& renames, const Scope& scope) {
    if (expression.kind == p::ExpressionKind::Name) {
        if (const auto found = renames.find(expression.name);
            found != renames.end() && !scope.values.contains(expression.name))
            expression.name = found->second;
    }
    for (p::Expression& child : expression.children)
        rename_expression(child, renames, scope);
    for (p::Expression& callee : expression.callee)
        rename_expression(callee, renames, scope);
    for (p::Expression& argument : expression.template_arguments)
        rename_expression(argument, renames, scope);
    for (p::TypeRef& argument : expression.type_arguments)
        rename_type(argument, renames, scope);
    if (expression.resolved_type)
        rename_type(*expression.resolved_type, renames, scope);
}

void rename_attribute(p::Attribute& attribute, const Renames& renames, const Scope& scope) {
    for (p::AttributeArgument& argument : attribute.arguments)
        rename_expression(argument.value, renames, scope);
}

void collect_pattern_bindings(const p::Expression& pattern, std::set<std::string>& bindings) {
    if (pattern.kind == p::ExpressionKind::Name) {
        bindings.insert(pattern.name);
        return;
    }
    if (pattern.kind == p::ExpressionKind::Member)
        return;
    if (pattern.kind == p::ExpressionKind::Call ||
        pattern.kind == p::ExpressionKind::TemplateCall) {
        for (const p::Expression& child : pattern.children)
            collect_pattern_bindings(child, bindings);
        return;
    }
    for (const p::Expression& child : pattern.children)
        collect_pattern_bindings(child, bindings);
}

void rename_statements(std::vector<p::Statement>& statements, const Renames& renames, Scope scope);

void rename_statement(p::Statement& statement, const Renames& renames, Scope& scope) {
    if (statement.type)
        rename_type(*statement.type, renames, scope);
    if (statement.expression)
        rename_expression(*statement.expression, renames, scope);
    if (statement.value)
        rename_expression(*statement.value, renames, scope);
    if (statement.target)
        rename_expression(*statement.target, renames, scope);
    if (statement.condition)
        rename_expression(*statement.condition, renames, scope);
    if (statement.message)
        rename_expression(*statement.message, renames, scope);
    if (statement.iterable)
        rename_expression(*statement.iterable, renames, scope);
    if (statement.pattern)
        rename_expression(*statement.pattern, renames, scope);
    if (statement.guard)
        rename_expression(*statement.guard, renames, scope);

    Scope children_scope = scope;
    if (statement.kind == p::StatementKind::For || statement.kind == p::StatementKind::Except) {
        children_scope.values.insert(statement.name);
    }
    if (statement.kind == p::StatementKind::Case && statement.pattern)
        collect_pattern_bindings(*statement.pattern, children_scope.values);
    rename_statements(statement.children, renames, std::move(children_scope));

    if (statement.kind == p::StatementKind::Variable)
        scope.values.insert(statement.name);
}

void rename_statements(std::vector<p::Statement>& statements, const Renames& renames, Scope scope) {
    for (p::Statement& statement : statements)
        rename_statement(statement, renames, scope);
}

void rename_generic(p::GenericParameter& parameter, const Renames& renames, const Scope& scope) {
    if (parameter.type)
        rename_type(*parameter.type, renames, scope);
    if (parameter.default_type)
        rename_type(*parameter.default_type, renames, scope);
    if (parameter.default_value)
        rename_expression(*parameter.default_value, renames, scope);
}

void rename_field(p::FieldDecl& field, const Renames& renames, const Scope& scope) {
    rename_type(field.type, renames, scope);
    if (field.value)
        rename_expression(*field.value, renames, scope);
    for (p::Attribute& attribute : field.attributes)
        rename_attribute(attribute, renames, scope);
}

void rename_function(p::FunctionDecl& function, const Renames& renames, Scope scope) {
    for (p::GenericParameter& parameter : function.generic_parameters)
        rename_generic(parameter, renames, scope);
    for (const p::GenericParameter& parameter : function.generic_parameters)
        scope.types.insert(parameter.name);
    for (p::Parameter& parameter : function.parameters) {
        rename_type(parameter.type, renames, scope);
        if (parameter.default_value)
            rename_expression(*parameter.default_value, renames, scope);
        scope.values.insert(parameter.name);
    }
    if (function.return_type)
        rename_type(*function.return_type, renames, scope);
    rename_statements(function.body, renames, scope);
    for (p::Attribute& attribute : function.attributes)
        rename_attribute(attribute, renames, scope);
}

void rename_constant(p::ConstantDecl& constant, const Renames& renames, const Scope& scope) {
    rename_type(constant.type, renames, scope);
    rename_expression(constant.value, renames, scope);
    for (p::Attribute& attribute : constant.attributes)
        rename_attribute(attribute, renames, scope);
}

void rename_class(p::ClassDecl& klass, const Renames& renames, Scope scope) {
    for (p::GenericParameter& parameter : klass.generic_parameters)
        rename_generic(parameter, renames, scope);
    for (const p::GenericParameter& parameter : klass.generic_parameters)
        scope.types.insert(parameter.name);
    for (p::TypeRef& base : klass.bases)
        rename_type(base, renames, scope);
    for (p::FieldDecl& field : klass.fields)
        rename_field(field, renames, scope);
    for (p::FunctionDecl& method : klass.methods)
        rename_function(method, renames, scope);
    for (p::ConstantDecl& constant : klass.constants)
        rename_constant(constant, renames, scope);
    for (p::ConstantDecl& field : klass.static_fields)
        rename_constant(field, renames, scope);
    for (p::Attribute& attribute : klass.attributes)
        rename_attribute(attribute, renames, scope);
}

void rename_enum(p::EnumDecl& en, const Renames& renames, Scope scope) {
    for (p::GenericParameter& parameter : en.generic_parameters)
        rename_generic(parameter, renames, scope);
    for (const p::GenericParameter& parameter : en.generic_parameters)
        scope.types.insert(parameter.name);
    if (en.underlying_type)
        rename_type(*en.underlying_type, renames, scope);
    for (p::EnumVariant& variant : en.variants) {
        for (p::FieldDecl& field : variant.fields)
            rename_field(field, renames, scope);
        if (variant.value)
            rename_expression(*variant.value, renames, scope);
        for (p::Attribute& attribute : variant.attributes)
            rename_attribute(attribute, renames, scope);
    }
    for (p::FunctionDecl& method : en.methods)
        rename_function(method, renames, scope);
    for (p::Attribute& attribute : en.attributes)
        rename_attribute(attribute, renames, scope);
}

void rename_declaration(p::Declaration& declaration, const Renames& renames) {
    Scope scope;
    if (declaration.class_decl)
        rename_class(*declaration.class_decl, renames, scope);
    if (declaration.enum_decl)
        rename_enum(*declaration.enum_decl, renames, scope);
    if (declaration.function_decl)
        rename_function(*declaration.function_decl, renames, scope);
    if (declaration.field_decl)
        rename_field(*declaration.field_decl, renames, scope);
    if (declaration.constant_decl)
        rename_constant(*declaration.constant_decl, renames, scope);
    if (declaration.implementation_decl) {
        rename_type(declaration.implementation_decl->contract, renames, scope);
        rename_type(declaration.implementation_decl->target, renames, scope);
        for (p::FunctionDecl& method : declaration.implementation_decl->methods)
            rename_function(method, renames, scope);
    }
}

std::string* private_sibling_name(p::Declaration& declaration) {
    if (declaration.class_decl && declaration.class_decl->visibility == p::Visibility::Private)
        return &declaration.class_decl->name;
    if (declaration.enum_decl && declaration.enum_decl->visibility == p::Visibility::Private)
        return &declaration.enum_decl->name;
    if (declaration.function_decl &&
        declaration.function_decl->visibility == p::Visibility::Private)
        return &declaration.function_decl->name;
    if (declaration.constant_decl &&
        declaration.constant_decl->visibility == p::Visibility::Private)
        return &declaration.constant_decl->name;
    return nullptr;
}

void apply_to_group(std::vector<p::GeneratedDeclaration>& group, const Renames& renames) {
    for (p::GeneratedDeclaration& generated : group)
        rename_declaration(generated.declaration, renames);
}

} // namespace

void apply_expansion_hygiene(p::Expansion& expansion, const std::string& macro_identity,
                             const std::string& target_module, const std::string& target_name,
                             const p::SourceRange& invocation) {
    const std::string seed = macro_identity + ":" + target_module + ":" + target_name + ":" +
                             invocation.file + ":" + std::to_string(invocation.start.line) + ":" +
                             std::to_string(invocation.start.column);
    Renames renames;
    for (p::GeneratedDeclaration& generated : expansion.siblings) {
        std::string* name = private_sibling_name(generated.declaration);
        if (name == nullptr)
            continue;
        if (name->empty())
            throw std::runtime_error("private generated sibling name cannot be empty");
        const std::string original = *name;
        const std::string hygienic_name = private_name(original, seed);
        if (!renames.emplace(original, hygienic_name).second)
            throw std::runtime_error("duplicate private generated sibling name: " + original);
        *name = hygienic_name;
    }
    if (renames.empty())
        return;
    apply_to_group(expansion.members, renames);
    apply_to_group(expansion.siblings, renames);
    apply_to_group(expansion.implementations, renames);
}

} // namespace dudu::macro
