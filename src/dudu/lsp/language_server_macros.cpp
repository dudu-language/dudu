#include "dudu/lsp/language_server_macros.hpp"

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/project/project_index.hpp"

#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

using MacroDefinition = macro::ExpansionReport::Definition;

bool position_on_name(const SourceLocation& location, std::string_view name,
                      const LspPosition& position) {
    if (location.line != position.line + 1 || location.column <= 0 || name.empty())
        return false;
    const int begin = location.column - 1;
    return position.character >= begin &&
           position.character <= begin + static_cast<int>(name.size());
}

std::optional<std::pair<std::string, SourceLocation>> path_at(const Expr& expression,
                                                              const LspPosition& position) {
    const std::optional<ExprPath> path = expr_path_from_expr(expression);
    if (!path)
        return std::nullopt;
    for (const ExprPathSegment& segment : path->segments) {
        if (position_on_name(segment.location, segment.text, position))
            return std::pair{render_expr_path(*path), segment.location};
    }
    return std::nullopt;
}

std::optional<std::string> decorator_reference(const Decorator& decorator) {
    const Expr& expression = decorator.expr;
    const Expr* callee = &expression;
    if ((expression.kind == ExprKind::Call || expression.kind == ExprKind::TemplateCall) &&
        has_expr_callee(expression)) {
        callee = &expr_callee(expression).front();
    }
    const std::optional<ExprPath> path = expr_path_from_expr(*callee);
    return path ? std::optional(render_expr_path(*path)) : std::nullopt;
}

std::optional<MacroEditorSelection> selection_for_decorator(const Decorator& decorator,
                                                            const LspPosition& position) {
    const Expr& expression = decorator.expr;
    const Expr* callee = &expression;
    if ((expression.kind == ExprKind::Call || expression.kind == ExprKind::TemplateCall) &&
        has_expr_callee(expression)) {
        callee = &expr_callee(expression).front();
    }
    if (const auto selected = path_at(*callee, position)) {
        return MacroEditorSelection{.kind = MacroEditorSelectionKind::Macro,
                                    .reference = selected->first,
                                    .option = {},
                                    .location = selected->second};
    }

    const std::optional<std::string> reference = decorator_reference(decorator);
    if (!reference || expression.kind != ExprKind::Call)
        return std::nullopt;
    if (*reference == "derive") {
        for (const Expr& argument : expression.children) {
            if (const auto selected = path_at(argument, position)) {
                return MacroEditorSelection{.kind = MacroEditorSelectionKind::Macro,
                                            .reference = selected->first,
                                            .option = {},
                                            .location = selected->second};
            }
        }
        return std::nullopt;
    }
    for (const Expr& argument : expression.children) {
        if (argument.kind == ExprKind::NamedArg &&
            position_on_name(argument.location, argument.name, position)) {
            return MacroEditorSelection{.kind = MacroEditorSelectionKind::HelperOption,
                                        .reference = *reference,
                                        .option = argument.name,
                                        .location = argument.location};
        }
    }
    return std::nullopt;
}

template <typename Declarations, typename Decorators>
std::optional<MacroEditorSelection>
selection_in(const Declarations& declarations, Decorators decorators, const LspPosition& position) {
    for (const auto& declaration : declarations) {
        for (const Decorator& decorator : decorators(declaration)) {
            if (auto selected = selection_for_decorator(decorator, position))
                return selected;
        }
    }
    return std::nullopt;
}

std::string resolved_import_module(const ModuleAst& current, const ImportDecl& import) {
    for (const ModuleDependency& dependency : current.dependencies) {
        if (dependency.kind == import.kind && dependency.import_module_path == import.module_path)
            return dependency.resolved_module_path;
    }
    return import.module_path;
}

const MacroDefinition* by_identity(const ProjectIndex& index, std::string_view identity) {
    for (const MacroDefinition& definition : index.macro_report().definitions) {
        if (definition.identity == identity)
            return &definition;
    }
    return nullptr;
}

const MacroDefinition* resolve(const ProjectIndex& index, const ModuleAst& current,
                               std::string_view reference) {
    if (const MacroDefinition* local =
            by_identity(index, current.module_path + "." + std::string(reference))) {
        return local;
    }
    for (const ImportDecl& import : current.imports) {
        if (import.kind == ImportKind::From) {
            const std::string exposed = bound_import_name(import);
            if (reference == exposed) {
                return by_identity(index, resolved_import_module(current, import) + "." +
                                              import.imported_name);
            }
            continue;
        }
        if (import.kind != ImportKind::Module)
            continue;
        const std::string prefix = import.alias.empty() ? import.module_path : import.alias;
        if (!reference.starts_with(prefix + "."))
            continue;
        return by_identity(index, resolved_import_module(current, import) + "." +
                                      std::string(reference.substr(prefix.size() + 1)));
    }
    return nullptr;
}

SourceLocation source_location(const macro::protocol::SourceRange& range) {
    return {.file = SourceFileName(range.file),
            .line = static_cast<int>(range.start.line),
            .column = static_cast<int>(range.start.column)};
}

std::string macro_detail(const MacroDefinition& definition) {
    std::ostringstream out;
    out << "@macro " << definition.name << "(" << definition.accepted_kind << ")";
    if (definition.attribute_schema && !definition.attribute_schema->fields.empty()) {
        out << "\noptions:";
        for (const macro::protocol::FieldDecl& field : definition.attribute_schema->fields) {
            out << "\n    " << field.name << ": "
                << type_ref_text(macro::from_protocol(field.type));
        }
    }
    return out.str();
}

std::optional<Symbol> option_symbol(const MacroDefinition& definition,
                                    const MacroEditorSelection& selection) {
    if (!definition.attribute_schema)
        return std::nullopt;
    for (const macro::protocol::FieldDecl& field : definition.attribute_schema->fields) {
        if (field.name != selection.option)
            continue;
        std::string detail = field.name + ": " + type_ref_text(macro::from_protocol(field.type));
        return Symbol{.name = field.name,
                      .detail = std::move(detail),
                      .location = source_location(field.range),
                      .kind = lsp_symbol_kind::Field,
                      .native_identity_key = std::nullopt,
                      .doc_comment = field.documentation};
    }
    return std::nullopt;
}

} // namespace

std::optional<MacroEditorSelection> macro_selection_at(const ModuleAst& module,
                                                       const Json* params) {
    if (params == nullptr)
        return std::nullopt;
    const LspPosition position = lsp_position(params);
    const auto plain = [&](const auto& declaration) -> const auto& {
        return declaration.decorators;
    };
    if (auto selected = selection_in(module.classes, plain, position))
        return selected;
    for (const ClassDecl& klass : module.classes) {
        if (auto selected = selection_in(klass.fields, plain, position))
            return selected;
        if (auto selected = selection_in(klass.constants, plain, position))
            return selected;
        if (auto selected = selection_in(klass.static_fields, plain, position))
            return selected;
        if (auto selected = selection_in(klass.methods, plain, position))
            return selected;
    }
    if (auto selected = selection_in(module.enums, plain, position))
        return selected;
    for (const EnumDecl& enumeration : module.enums) {
        if (auto selected = selection_in(enumeration.values, plain, position))
            return selected;
        if (auto selected = selection_in(enumeration.methods, plain, position))
            return selected;
    }
    if (auto selected = selection_in(module.functions, plain, position))
        return selected;
    return selection_in(module.constants, plain, position);
}

std::optional<Symbol> macro_symbol_for_reference(const ProjectIndex& index,
                                                 const ModuleAst& current,
                                                 const MacroEditorSelection& selection) {
    const MacroDefinition* definition = resolve(index, current, selection.reference);
    if (definition == nullptr)
        return std::nullopt;
    if (selection.kind == MacroEditorSelectionKind::HelperOption)
        return option_symbol(*definition, selection);
    return Symbol{.name = "@" + definition->name,
                  .detail = macro_detail(*definition),
                  .location = source_location(definition->location),
                  .kind = lsp_symbol_kind::Function,
                  .native_identity_key = std::nullopt,
                  .doc_comment = definition->documentation};
}

bool macro_reference_resolves(const ProjectIndex& index, const ModuleAst& current,
                              std::string_view reference) {
    return resolve(index, current, reference) != nullptr;
}

std::optional<MacroEditorCall> macro_call_for_reference(const ProjectIndex& index,
                                                        const ModuleAst& current,
                                                        std::string_view reference) {
    const MacroDefinition* definition = resolve(index, current, reference);
    if (definition == nullptr)
        return std::nullopt;
    MacroEditorCall call{.name = definition->name,
                         .signature = "@" + definition->name + "(",
                         .documentation = definition->documentation,
                         .options = {}};
    if (definition->attribute_schema) {
        for (const macro::protocol::FieldDecl& field : definition->attribute_schema->fields) {
            const std::string type = type_ref_text(macro::from_protocol(field.type));
            call.options.push_back({.name = field.name,
                                    .type = type,
                                    .documentation = field.documentation,
                                    .required = !field.value.has_value()});
            if (call.options.size() > 1)
                call.signature += ", ";
            call.signature += field.name + ": " + type;
            if (field.value)
                call.signature += " = ...";
        }
    }
    call.signature += ")";
    return call;
}

} // namespace dudu
