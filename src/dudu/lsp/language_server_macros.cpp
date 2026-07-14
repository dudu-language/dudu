#include "dudu/lsp/language_server_macros.hpp"

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/project/project_index.hpp"

#include <optional>
#include <set>
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

const MacroDefinition* definition_for_identity(const ProjectIndex& index,
                                               std::string_view identity) {
    return by_identity(index, identity);
}

bool same_source_file(const SourceLocation& location, const std::filesystem::path& path) {
    if (location.file.empty())
        return false;
    return same_path(std::filesystem::path(location.file.str()), path);
}

bool position_on_range(const SourceRange& range, const LspPosition& position) {
    if (range.start.line <= 0 || range.start.column <= 0 || range.end.line <= 0 ||
        range.end.column <= 0)
        return false;
    const int line = position.line + 1;
    const int column = position.character + 1;
    if (line < range.start.line || line > range.end.line)
        return false;
    if (line == range.start.line && column < range.start.column)
        return false;
    return line != range.end.line || column <= range.end.column;
}

SourceRange source_range(const macro::protocol::SourceRange& range) {
    return macro::from_protocol(range);
}

void add_reference(std::vector<ReferenceLocation>& out, std::set<std::pair<int, int>>& seen,
                   const Document& document, const SourceLocation& location,
                   std::string_view text) {
    if (text.empty() || location.line <= 0 || location.column <= 0 ||
        !seen.insert({location.line, location.column}).second)
        return;
    SourceRange range{.start = location, .end = location};
    range.end.column += static_cast<int>(text.size());
    out.push_back({.uri = uri_for_location(location, document),
                   .range = range_json(range),
                   .source_range = range});
}

template <typename Visit>
void visit_decorator_macro_references(const Decorator& decorator, Visit&& visit) {
    const Expr& expression = decorator.expr;
    const Expr* callee = &expression;
    if ((expression.kind == ExprKind::Call || expression.kind == ExprKind::TemplateCall) &&
        has_expr_callee(expression)) {
        callee = &expr_callee(expression).front();
    }
    const std::optional<ExprPath> callee_path = expr_path_from_expr(*callee);
    if (!callee_path || callee_path->segments.empty())
        return;
    const std::string reference = render_expr_path(*callee_path);
    if (reference == "derive" && expression.kind == ExprKind::Call) {
        for (const Expr& argument : expression.children) {
            const std::optional<ExprPath> path = expr_path_from_expr(argument);
            if (path && !path->segments.empty())
                visit(render_expr_path(*path), path->segments.back());
        }
        return;
    }
    visit(reference, callee_path->segments.back());
}

template <typename Declarations, typename Decorators, typename Visit>
void visit_declaration_decorators(const Declarations& declarations, Decorators decorators,
                                  Visit&& visit) {
    for (const auto& declaration : declarations) {
        for (const Decorator& decorator : decorators(declaration))
            visit_decorator_macro_references(decorator, visit);
    }
}

template <typename Visit> void visit_module_macro_references(const ModuleAst& module, Visit visit) {
    const auto decorators = [&](const auto& declaration) -> const auto& {
        return declaration.decorators;
    };
    visit_declaration_decorators(module.classes, decorators, visit);
    for (const ClassDecl& klass : module.classes) {
        visit_declaration_decorators(klass.fields, decorators, visit);
        visit_declaration_decorators(klass.constants, decorators, visit);
        visit_declaration_decorators(klass.static_fields, decorators, visit);
        visit_declaration_decorators(klass.methods, decorators, visit);
    }
    visit_declaration_decorators(module.enums, decorators, visit);
    for (const EnumDecl& enumeration : module.enums) {
        visit_declaration_decorators(enumeration.values, decorators, visit);
        visit_declaration_decorators(enumeration.methods, decorators, visit);
    }
    visit_declaration_decorators(module.functions, decorators, visit);
    visit_declaration_decorators(module.constants, decorators, visit);
}

const GeneratedDeclarationOrigin* generated_origin(const ModuleAst& module,
                                                   std::string_view reference) {
    const size_t final_dot = reference.rfind('.');
    const std::string_view name =
        final_dot == std::string_view::npos ? reference : reference.substr(final_dot + 1);
    const GeneratedDeclarationOrigin* match = nullptr;
    for (const GeneratedDeclarationOrigin& origin : module.generated_origins) {
        if (origin.name != name)
            continue;
        if (match != nullptr)
            return nullptr;
        match = &origin;
    }
    return match;
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

std::optional<MacroReferenceTarget>
macro_reference_target_at(const ProjectIndex& index, const ModuleAst& current, const Json* params) {
    if (const std::optional<MacroEditorSelection> selected = macro_selection_at(current, params)) {
        if (const MacroDefinition* definition = resolve(index, current, selected->reference))
            return MacroReferenceTarget{.identity = definition->identity, .name = definition->name};
    }
    if (params == nullptr)
        return std::nullopt;
    const LspPosition position = lsp_position(params);
    for (const MacroDefinition& definition : index.macro_report().definitions) {
        const SourceRange range = source_range(definition.location);
        if (same_source_file(range.start, current.source_path) &&
            position_on_range(range, position))
            return MacroReferenceTarget{.identity = definition.identity, .name = definition.name};
    }
    for (const ImportDecl& import : current.imports) {
        if (import.kind != ImportKind::From)
            continue;
        const MacroDefinition* definition = resolve(index, current, bound_import_name(import));
        if (definition == nullptr)
            continue;
        if (position_on_range(import.imported_name_range, position) ||
            (!import.alias.empty() && position_on_range(import.alias_range, position))) {
            return MacroReferenceTarget{.identity = definition->identity, .name = definition->name};
        }
    }
    return std::nullopt;
}

std::vector<ReferenceLocation> macro_reference_locations(const ProjectIndex& index,
                                                         const ModuleAst& current,
                                                         const Document& document,
                                                         std::string_view identity) {
    std::vector<ReferenceLocation> out;
    std::set<std::pair<int, int>> seen;
    const MacroDefinition* target = definition_for_identity(index, identity);
    if (target == nullptr)
        return out;
    const SourceRange definition_range = source_range(target->location);
    if (same_source_file(definition_range.start, document.path))
        add_reference(out, seen, document, definition_range.start, target->name);
    for (const ImportDecl& import : current.imports) {
        if (import.kind != ImportKind::From)
            continue;
        const MacroDefinition* imported = resolve(index, current, bound_import_name(import));
        if (imported == nullptr || imported->identity != identity)
            continue;
        add_reference(out, seen, document, import.imported_name_range.start, import.imported_name);
        if (!import.alias.empty())
            add_reference(out, seen, document, import.alias_range.start, import.alias);
    }
    visit_module_macro_references(
        current, [&](const std::string& reference, const ExprPathSegment& segment) {
            const MacroDefinition* referenced = resolve(index, current, reference);
            if (referenced != nullptr && referenced->identity == identity)
                add_reference(out, seen, document, segment.location, segment.text);
        });
    return out;
}

Symbol with_macro_generated_origin(const ModuleAst& module, std::string_view reference,
                                   Symbol symbol) {
    const GeneratedDeclarationOrigin* origin = generated_origin(module, reference);
    if (origin == nullptr)
        return symbol;
    if (!symbol.doc_comment.empty())
        symbol.doc_comment += "\n\n";
    symbol.doc_comment +=
        "Generated by `@" + origin->macro_name + "` (`" + origin->macro_identity + "`).";
    return symbol;
}

std::optional<Symbol> macro_generated_symbol_for_reference(const ModuleAst& module,
                                                           std::string_view reference) {
    const GeneratedDeclarationOrigin* origin = generated_origin(module, reference);
    if (origin == nullptr)
        return std::nullopt;
    const auto function_symbol = [&](const FunctionDecl& function, int kind) {
        return with_macro_generated_origin(module, reference,
                                           Symbol{.name = function.name,
                                                  .detail = function_detail(function),
                                                  .location = function.location,
                                                  .kind = kind,
                                                  .native_identity_key = std::nullopt,
                                                  .doc_comment = function.doc_comment});
    };
    if (origin->owner.empty()) {
        for (const FunctionDecl& function : module.functions) {
            if (function.name == origin->name)
                return function_symbol(function, lsp_symbol_kind::Function);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        if (klass.name != origin->owner)
            continue;
        for (const FunctionDecl& method : klass.methods) {
            if (method.name == origin->name)
                return function_symbol(method, lsp_symbol_kind::Method);
        }
        for (const FieldDecl& field : klass.fields) {
            if (field.name == origin->name) {
                return with_macro_generated_origin(
                    module, reference,
                    Symbol{.name = field.name,
                           .detail = field.name + ": " + type_ref_text(field.type_ref),
                           .location = field.location,
                           .kind = lsp_symbol_kind::Field,
                           .native_identity_key = std::nullopt,
                           .doc_comment = field.doc_comment});
            }
        }
    }
    for (const EnumDecl& enumeration : module.enums) {
        if (enumeration.name != origin->owner)
            continue;
        for (const FunctionDecl& method : enumeration.methods) {
            if (method.name == origin->name)
                return function_symbol(method, lsp_symbol_kind::Method);
        }
    }
    return std::nullopt;
}

std::optional<SourceLocation> macro_generated_definition_location(const ModuleAst& module,
                                                                  std::string_view reference) {
    const GeneratedDeclarationOrigin* origin = generated_origin(module, reference);
    return origin == nullptr ? std::nullopt : std::optional(origin->invocation.start);
}

} // namespace dudu
