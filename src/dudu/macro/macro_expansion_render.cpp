#include "dudu/macro/macro_expansion_render.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace dudu::macro {
namespace {

namespace p = protocol;

std::string join(const std::vector<std::string>& values, std::string_view separator) {
    std::ostringstream out;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            out << separator;
        out << values[index];
    }
    return out.str();
}

std::string quote_string(std::string_view value) {
    std::string out = "\"";
    for (const char c : value) {
        if (c == '\\' || c == '"')
            out.push_back('\\');
        if (c == '\n') {
            out += "\\n";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string render_type(const p::TypeRef& type);
std::string render_expression(const p::Expression& expression);

std::vector<std::string> rendered_types(const std::vector<p::TypeRef>& values) {
    std::vector<std::string> out;
    out.reserve(values.size());
    for (const p::TypeRef& value : values)
        out.push_back(render_type(value));
    return out;
}

std::string render_type(const p::TypeRef& type) {
    const auto child = [&]() {
        return type.children.empty() ? "?" : render_type(type.children[0]);
    };
    switch (type.kind) {
    case p::TypeKind::Named:
    case p::TypeKind::Qualified:
    case p::TypeKind::Associated:
        return type.name;
    case p::TypeKind::Value:
        return type.value;
    case p::TypeKind::Template:
        return type.name + "[" + join(rendered_types(type.children), ", ") + "]";
    case p::TypeKind::Pointer:
        return "*" + child();
    case p::TypeKind::Reference:
        return "&" + child();
    case p::TypeKind::Const:
        return "const[" + child() + "]";
    case p::TypeKind::Volatile:
        return "volatile[" + child() + "]";
    case p::TypeKind::Atomic:
        return "atomic[" + child() + "]";
    case p::TypeKind::Device:
        return "device[" + child() + "]";
    case p::TypeKind::Storage:
        return "storage[" + child() + "]";
    case p::TypeKind::Shared:
        return "shared[" + child() + "]";
    case p::TypeKind::Static:
        return "static[" + child() + "]";
    case p::TypeKind::FixedArray:
    case p::TypeKind::Shaped: {
        if (type.children.empty())
            return "array[?]";
        std::vector<std::string> shape;
        for (std::size_t index = 1; index < type.children.size(); ++index)
            shape.push_back(render_type(type.children[index]));
        return "array[" + render_type(type.children[0]) + "][" + join(shape, ", ") + "]";
    }
    case p::TypeKind::Function: {
        if (type.children.empty())
            return "fn()";
        std::vector<std::string> arguments;
        for (std::size_t index = 0; index + 1 < type.children.size(); ++index)
            arguments.push_back(render_type(type.children[index]));
        return "fn(" + join(arguments, ", ") + ") -> " + render_type(type.children.back());
    }
    case p::TypeKind::PackExpansion:
        return child() + "...";
    case p::TypeKind::Unknown:
        return "?";
    }
    return "?";
}

std::vector<std::string> rendered_expressions(const std::vector<p::Expression>& values) {
    std::vector<std::string> out;
    out.reserve(values.size());
    for (const p::Expression& value : values)
        out.push_back(render_expression(value));
    return out;
}

std::string render_expression(const p::Expression& expression) {
    const auto child = [&](std::size_t index) {
        return index < expression.children.size() ? render_expression(expression.children[index])
                                                  : std::string("?");
    };
    switch (expression.kind) {
    case p::ExpressionKind::Name:
        return expression.name;
    case p::ExpressionKind::BoolLiteral:
    case p::ExpressionKind::IntLiteral:
    case p::ExpressionKind::FloatLiteral:
        return expression.value;
    case p::ExpressionKind::StringLiteral:
        return quote_string(expression.value);
    case p::ExpressionKind::NoneLiteral:
        return "None";
    case p::ExpressionKind::Unary:
        return expression.operator_name + child(0);
    case p::ExpressionKind::Binary:
        return child(0) + " " + expression.operator_name + " " + child(1);
    case p::ExpressionKind::Call:
    case p::ExpressionKind::TemplateCall: {
        const std::string callee = expression.callee.empty()
                                       ? expression.name
                                       : render_expression(expression.callee.front());
        std::string types;
        if (!expression.type_arguments.empty())
            types = "[" + join(rendered_types(expression.type_arguments), ", ") + "]";
        return callee + types + "(" + join(rendered_expressions(expression.children), ", ") + ")";
    }
    case p::ExpressionKind::Member:
        return child(0) + "." + expression.name;
    case p::ExpressionKind::Index: {
        std::vector<std::string> indices;
        for (std::size_t index = 1; index < expression.children.size(); ++index)
            indices.push_back(render_expression(expression.children[index]));
        return child(0) + "[" + join(indices, ", ") + "]";
    }
    case p::ExpressionKind::ListLiteral:
        return "[" + join(rendered_expressions(expression.children), ", ") + "]";
    case p::ExpressionKind::SetLiteral:
        return "{" + join(rendered_expressions(expression.children), ", ") + "}";
    case p::ExpressionKind::TupleLiteral:
        return "(" + join(rendered_expressions(expression.children), ", ") + ")";
    case p::ExpressionKind::DictEntry:
        return child(0) + ": " + child(1);
    case p::ExpressionKind::DictLiteral:
        return "{" + join(rendered_expressions(expression.children), ", ") + "}";
    case p::ExpressionKind::NamedArg:
        return expression.name + "=" + child(0);
    case p::ExpressionKind::Slice:
        return child(0) + ":" + child(1) + (expression.children.size() > 2 ? ":" + child(2) : "");
    case p::ExpressionKind::Ellipsis:
        return "...";
    case p::ExpressionKind::NewAxis:
        return "None";
    case p::ExpressionKind::PackExpansion:
        return child(0) + "...";
    case p::ExpressionKind::CppEscape:
        return "cpp(" + quote_string(expression.value) + ")";
    case p::ExpressionKind::Missing:
        return "";
    }
    return "?";
}

std::string indentation(int depth) {
    return std::string(static_cast<std::size_t>(depth) * 4, ' ');
}

void render_statements(std::ostringstream& out, const std::vector<p::Statement>& statements,
                       int depth) {
    for (const p::Statement& statement : statements) {
        const std::string pad = indentation(depth);
        const auto expr = [&](const std::optional<p::Expression>& value) {
            return value ? render_expression(*value) : std::string{};
        };
        switch (statement.kind) {
        case p::StatementKind::Expression:
            out << pad << expr(statement.expression) << '\n';
            break;
        case p::StatementKind::Variable:
            out << pad << statement.name;
            if (statement.type)
                out << ": " << render_type(*statement.type);
            if (statement.value)
                out << " = " << expr(statement.value);
            out << '\n';
            break;
        case p::StatementKind::Assign:
            out << pad << expr(statement.target) << " = " << expr(statement.value) << '\n';
            break;
        case p::StatementKind::CompoundAssign:
            out << pad << expr(statement.target) << ' ' << statement.operator_name << "= "
                << expr(statement.value) << '\n';
            break;
        case p::StatementKind::Return:
            out << pad << "return" << (statement.value ? " " + expr(statement.value) : "") << '\n';
            break;
        case p::StatementKind::If:
        case p::StatementKind::Elif:
        case p::StatementKind::While: {
            const char* word = statement.kind == p::StatementKind::If     ? "if"
                               : statement.kind == p::StatementKind::Elif ? "elif"
                                                                          : "while";
            out << pad << word << ' ' << expr(statement.condition) << ":\n";
            render_statements(out, statement.children, depth + 1);
            break;
        }
        case p::StatementKind::Else:
        case p::StatementKind::Try:
            out << pad << (statement.kind == p::StatementKind::Else ? "else" : "try") << ":\n";
            render_statements(out, statement.children, depth + 1);
            break;
        case p::StatementKind::Match:
            out << pad << "match " << expr(statement.expression) << ":\n";
            render_statements(out, statement.children, depth + 1);
            break;
        case p::StatementKind::Case:
            out << pad << "case " << expr(statement.pattern);
            if (statement.guard)
                out << " if " << expr(statement.guard);
            out << ":\n";
            render_statements(out, statement.children, depth + 1);
            break;
        case p::StatementKind::For:
            out << pad << "for " << statement.name;
            if (statement.type)
                out << ": " << render_type(*statement.type);
            out << " in " << expr(statement.iterable) << ":\n";
            render_statements(out, statement.children, depth + 1);
            break;
        case p::StatementKind::Except:
            out << pad << "except";
            if (statement.type)
                out << ' ' << render_type(*statement.type);
            if (!statement.name.empty())
                out << " as " << statement.name;
            out << ":\n";
            render_statements(out, statement.children, depth + 1);
            break;
        case p::StatementKind::Raise:
            out << pad << "raise " << expr(statement.expression) << '\n';
            break;
        case p::StatementKind::Delete:
            out << pad << "delete " << expr(statement.target) << '\n';
            break;
        case p::StatementKind::Assert:
        case p::StatementKind::DebugAssert:
            out << pad << (statement.kind == p::StatementKind::Assert ? "assert " : "debug_assert ")
                << expr(statement.condition);
            if (statement.message)
                out << ", " << expr(statement.message);
            out << '\n';
            break;
        case p::StatementKind::Break:
            out << pad << "break\n";
            break;
        case p::StatementKind::Continue:
            out << pad << "continue\n";
            break;
        case p::StatementKind::Pass:
            out << pad << "pass\n";
            break;
        case p::StatementKind::CppEscape:
            out << pad << "cpp(" << quote_string(expr(statement.expression)) << ")\n";
            break;
        case p::StatementKind::Unknown:
            out << pad << "# unknown generated statement\n";
            break;
        }
    }
}

void render_attributes(std::ostringstream& out, const std::vector<p::Attribute>& attributes,
                       int depth) {
    for (const p::Attribute& attribute : attributes) {
        out << indentation(depth) << '@' << attribute.name;
        if (!attribute.arguments.empty()) {
            std::vector<std::string> arguments;
            for (const p::AttributeArgument& argument : attribute.arguments) {
                arguments.push_back((argument.name.empty() ? "" : argument.name + "=") +
                                    render_expression(argument.value));
            }
            out << '(' << join(arguments, ", ") << ')';
        }
        out << '\n';
    }
}

std::string generic_suffix(const std::vector<p::GenericParameter>& parameters) {
    std::vector<std::string> values;
    for (const p::GenericParameter& parameter : parameters) {
        std::string value = parameter.name + (parameter.variadic ? "..." : "");
        if (parameter.type)
            value += ": " + render_type(*parameter.type);
        if (parameter.default_type)
            value += " = " + render_type(*parameter.default_type);
        if (parameter.default_value)
            value += " = " + render_expression(*parameter.default_value);
        values.push_back(std::move(value));
    }
    return values.empty() ? "" : "[" + join(values, ", ") + "]";
}

void render_function(std::ostringstream& out, const p::FunctionDecl& function, int depth) {
    render_attributes(out, function.attributes, depth);
    std::vector<std::string> parameters;
    for (const p::Parameter& parameter : function.parameters) {
        std::string value =
            parameter.name + (parameter.variadic ? "..." : "") + ": " + render_type(parameter.type);
        if (parameter.default_value)
            value += " = " + render_expression(*parameter.default_value);
        parameters.push_back(std::move(value));
    }
    out << indentation(depth) << "def " << function.name
        << generic_suffix(function.generic_parameters) << '(' << join(parameters, ", ") << ')';
    if (function.return_type)
        out << " -> " << render_type(*function.return_type);
    out << ":\n";
    if (function.body.empty()) {
        out << indentation(depth + 1) << "pass\n";
    } else {
        render_statements(out, function.body, depth + 1);
    }
}

void render_field(std::ostringstream& out, const p::FieldDecl& field, int depth) {
    render_attributes(out, field.attributes, depth);
    out << indentation(depth) << field.name << ": " << render_type(field.type);
    if (field.value)
        out << " = " << render_expression(*field.value);
    out << '\n';
}

void render_declaration(std::ostringstream& out, const p::Declaration& declaration, int depth) {
    if (declaration.function_decl) {
        render_function(out, *declaration.function_decl, depth);
    } else if (declaration.field_decl) {
        render_field(out, *declaration.field_decl, depth);
    } else if (declaration.constant_decl) {
        const p::ConstantDecl& value = *declaration.constant_decl;
        render_attributes(out, value.attributes, depth);
        out << indentation(depth) << value.name << ": " << render_type(value.type) << " = "
            << render_expression(value.value) << '\n';
    } else if (declaration.class_decl) {
        const p::ClassDecl& value = *declaration.class_decl;
        render_attributes(out, value.attributes, depth);
        out << indentation(depth) << "class " << value.name
            << generic_suffix(value.generic_parameters);
        if (!value.bases.empty())
            out << '(' << join(rendered_types(value.bases), ", ") << ')';
        out << ":\n";
        if (value.fields.empty() && value.constants.empty() && value.static_fields.empty() &&
            value.methods.empty()) {
            out << indentation(depth + 1) << "pass\n";
        }
        for (const p::FieldDecl& field : value.fields)
            render_field(out, field, depth + 1);
        for (const p::ConstantDecl& constant : value.constants) {
            p::Declaration child{.kind = p::DeclarationKind::Constant, .constant_decl = constant};
            render_declaration(out, child, depth + 1);
        }
        for (const p::ConstantDecl& field : value.static_fields) {
            p::Declaration child{.kind = p::DeclarationKind::Constant, .constant_decl = field};
            render_declaration(out, child, depth + 1);
        }
        for (const p::FunctionDecl& method : value.methods)
            render_function(out, method, depth + 1);
    } else if (declaration.enum_decl) {
        const p::EnumDecl& value = *declaration.enum_decl;
        render_attributes(out, value.attributes, depth);
        out << indentation(depth) << "enum " << value.name
            << generic_suffix(value.generic_parameters) << ":\n";
        for (const p::EnumVariant& variant : value.variants) {
            render_attributes(out, variant.attributes, depth + 1);
            out << indentation(depth + 1) << variant.name;
            if (variant.value)
                out << " = " << render_expression(*variant.value);
            out << (variant.fields.empty() ? "\n" : ":\n");
            for (const p::FieldDecl& field : variant.fields)
                render_field(out, field, depth + 2);
        }
        for (const p::FunctionDecl& method : value.methods)
            render_function(out, method, depth + 1);
    } else if (declaration.implementation_decl) {
        const p::ImplementationDecl& value = *declaration.implementation_decl;
        out << indentation(depth) << "impl " << render_type(value.contract) << " for "
            << render_type(value.target) << ":\n";
        for (const p::FunctionDecl& method : value.methods)
            render_function(out, method, depth + 1);
    }
}

void render_group(std::ostringstream& out, std::string_view label,
                  const std::vector<p::GeneratedDeclaration>& declarations) {
    if (declarations.empty())
        return;
    out << "# " << label << '\n';
    for (const p::GeneratedDeclaration& generated : declarations) {
        render_declaration(out, generated.declaration, 0);
        out << '\n';
    }
}

} // namespace

std::string render_expansion_report(const ExpansionReport& report) {
    std::ostringstream out;
    for (const ExpansionReport::Record& record : report.expansions) {
        out << "# @" << record.macro_name << " (" << record.macro_identity << ") for "
            << record.target_module << '.' << record.target_name << '\n';
        if (!record.invocation.file.empty()) {
            out << "# invoked at " << record.invocation.file << ':' << record.invocation.start.line
                << ':' << record.invocation.start.column << '\n';
        }
        render_group(out, "generated members", record.expansion.members);
        render_group(out, "generated siblings", record.expansion.siblings);
        render_group(out, "generated implementations", record.expansion.implementations);
        if (record.expansion.members.empty() && record.expansion.siblings.empty() &&
            record.expansion.implementations.empty()) {
            out << "# no declarations generated\n\n";
        }
    }
    if (report.expansions.empty())
        out << "# no macro expansions\n";
    return out.str();
}

} // namespace dudu::macro
