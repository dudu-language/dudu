#include "dudu/lsp/language_server_generated_cpp.hpp"

#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_classes.hpp"
#include "dudu/codegen/cpp_emit_enum_methods.hpp"
#include "dudu/codegen/cpp_emit_enums.hpp"
#include "dudu/codegen/cpp_emit_functions.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/codegen/cpp_emit_prelude.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_module_emit_context.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/ast_visit.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_types.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema_context.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace dudu {
namespace {

struct RequestedRange {
    SourceRange source;
    bool empty = true;
};

struct ExpressionSelection {
    const Expr* expression = nullptr;
    const FunctionDecl* function = nullptr;
    const ClassDecl* owner_class = nullptr;
    const EnumDecl* owner_enum = nullptr;
};

bool position_less(SourcePosition left, SourcePosition right) {
    return left.line < right.line || (left.line == right.line && left.column < right.column);
}

bool position_less_equal(SourcePosition left, SourcePosition right) {
    return !position_less(right, left);
}

bool valid_range(const SourceRange& range) {
    return range.start.line > 0 && range.start.column > 0 && range.end.line > 0 &&
           range.end.column > 0 &&
           position_less_equal(SourcePosition(range.start), range.end);
}

bool range_contains_position(const SourceRange& range, SourcePosition position) {
    return valid_range(range) && position_less_equal(SourcePosition(range.start), position) &&
           position_less_equal(position, range.end);
}

bool range_contains_range(const SourceRange& outer, const SourceRange& inner) {
    return valid_range(outer) && valid_range(inner) &&
           range_contains_position(outer, SourcePosition(inner.start)) &&
           range_contains_position(outer, inner.end);
}

bool narrower_than(const SourceRange& candidate, const SourceRange& current) {
    return !valid_range(current) ||
           (range_contains_range(current, candidate) && !range_contains_range(candidate, current));
}

bool better_declaration_range(const SourceRange& candidate, const SourceRange& current);

RequestedRange requested_range(const Document& document, const Json* argument) {
    const Json* range = argument == nullptr ? nullptr : argument->get("range");
    const Json* start = range == nullptr ? nullptr : range->get("start");
    const Json* end = range == nullptr ? nullptr : range->get("end");
    RequestedRange out;
    out.source.start.file = SourceFileName(document.path.string());
    out.source.start.line = required_int_value(start == nullptr ? nullptr : start->get("line"),
                                               "range.start.line") +
                            1;
    out.source.start.column =
        required_int_value(start == nullptr ? nullptr : start->get("character"),
                           "range.start.character") +
        1;
    out.source.end.line =
        required_int_value(end == nullptr ? nullptr : end->get("line"), "range.end.line") + 1;
    out.source.end.column = required_int_value(
                                end == nullptr ? nullptr : end->get("character"),
                                "range.end.character") +
                            1;
    out.empty = out.source.start.line == out.source.end.line &&
                out.source.start.column == out.source.end.column;
    return out;
}

CppModuleMap module_map(const ModuleAst& root) {
    CppModuleMap out;
    if (root.module_units.empty()) {
        out[root.module_path] = &root;
        return out;
    }
    for (const ModuleAst& unit : root.module_units) {
        out[unit.module_path] = &unit;
    }
    return out;
}

void consider_expression(const Expr& expression, const RequestedRange& request,
                         const FunctionDecl& function, const ClassDecl* owner_class,
                         const EnumDecl* owner_enum, ExpressionSelection& best,
                         SourceRange& best_range) {
    if (!valid_range(expression.range)) {
        return;
    }
    const bool selected = request.empty
                              ? range_contains_position(expression.range,
                                                        SourcePosition(request.source.start))
                              : range_contains_range(expression.range, request.source);
    if (!selected || !narrower_than(expression.range, best_range)) {
        return;
    }
    best = {.expression = &expression,
            .function = &function,
            .owner_class = owner_class,
            .owner_enum = owner_enum};
    best_range = expression.range;
}

void inspect_function_expressions(const FunctionDecl& function, const RequestedRange& request,
                                  const ClassDecl* owner_class, const EnumDecl* owner_enum,
                                  ExpressionSelection& best, SourceRange& best_range) {
    for (const Stmt& statement : function.statements) {
        visit_stmt_tree_expressions(statement, [&](const Expr& expression) {
            consider_expression(expression, request, function, owner_class, owner_enum, best,
                                best_range);
        });
    }
}

ExpressionSelection expression_selection(const ModuleAst& module,
                                         const RequestedRange& request) {
    ExpressionSelection best;
    SourceRange best_range;
    for (const FunctionDecl& function : module.functions) {
        inspect_function_expressions(function, request, nullptr, nullptr, best, best_range);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            inspect_function_expressions(method, request, &klass, nullptr, best, best_range);
        }
    }
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            inspect_function_expressions(method, request, nullptr, &en, best, best_range);
        }
    }
    return best;
}

ExpressionSelection expression_owner(const ModuleAst& module, const Expr& expression) {
    ExpressionSelection selected{.expression = &expression};
    SourceRange selected_range;
    const SourcePosition position(expression.location);
    const auto consider = [&](const FunctionDecl& function, const ClassDecl* owner_class,
                              const EnumDecl* owner_enum) {
        if (!range_contains_position(function.range, position) ||
            !better_declaration_range(function.range, selected_range)) {
            return;
        }
        selected.function = &function;
        selected.owner_class = owner_class;
        selected.owner_enum = owner_enum;
        selected_range = function.range;
    };
    for (const FunctionDecl& function : module.functions) {
        consider(function, nullptr, nullptr);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            consider(method, &klass, nullptr);
        }
    }
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            consider(method, nullptr, &en);
        }
    }
    return selected;
}

bool request_starts_in(const RequestedRange& request, const SourceRange& range) {
    return range_contains_position(range, SourcePosition(request.source.start));
}

std::string expression_source_name(const ExpressionSelection& selection) {
    if (selection.function == nullptr) {
        return "expression";
    }
    if (selection.owner_class != nullptr) {
        return selection.owner_class->name + "." + selection.function->name + " expression";
    }
    if (selection.owner_enum != nullptr) {
        return selection.owner_enum->name + "." + selection.function->name + " expression";
    }
    return selection.function->name + " expression";
}

CppLocalContext expression_locals(const ExpressionSelection& selection,
                                  const CppEmitOptions& options,
                                  const std::map<std::string, TypeRef>& local_types) {
    CppLocalContext locals;
    for (const auto& [name, type] : local_types) {
        (void)type;
        locals.bind(name);
    }
    if (selection.owner_class != nullptr) {
        locals.current_class = emitted_name(*selection.owner_class, options);
        if (selection.owner_class->base_class_refs.size() == 1) {
            locals.super_class = lower_cpp_type(
                selection.owner_class->base_class_refs.front().type_ref, options);
        }
    }
    return locals;
}

std::string emit_expression(const ModuleAst& module, const ExpressionSelection& selection,
                            const CppEmitOptions& options) {
    const std::vector<std::string> aliases = namespace_aliases(module);
    const Symbols symbols = collect_symbols(module);
    const std::map<std::string, TypeRef> local_types =
        local_type_refs_before_location(module, selection.expression->location);
    const CppLocalContext locals = expression_locals(selection, options, local_types);
    return lower_expr(*selection.expression, aliases, locals, local_types, &symbols, options);
}

std::string emit_function(const ModuleAst& module, const FunctionDecl& function,
                          const CppEmitOptions& options) {
    std::ostringstream out;
    emit_cpp_function_body(out, function, namespace_aliases(module),
                           cpp_function_return_types(module), collect_symbols(module), options);
    return out.str();
}

std::string emit_class(const ModuleAst& module, ClassDecl klass,
                       const FunctionDecl* selected_method, const CppEmitOptions& options) {
    if (selected_method != nullptr) {
        klass.methods = {*selected_method};
    }
    ModuleAst selected;
    selected.classes = {std::move(klass)};
    std::ostringstream out;
    emit_classes(out, selected, namespace_aliases(module), cpp_function_return_types(module),
                 collect_symbols(module), false, options);
    return out.str();
}

std::string emit_enum(const ModuleAst& module, EnumDecl en,
                      const FunctionDecl* selected_method, const CppEmitOptions& options) {
    if (selected_method != nullptr) {
        en.methods = {*selected_method};
    }
    ModuleAst selected;
    selected.enums = {std::move(en)};
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const auto function_returns = cpp_function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    emit_enum_method_declarations(out, selected, aliases, false, options);
    emit_enums(out, selected, aliases, options);
    emit_enum_method_definitions(out, selected, aliases, function_returns, symbols, false, options);
    return out.str();
}

std::string emit_constant(const ModuleAst& module, const ConstDecl& constant,
                          const CppEmitOptions& options) {
    ModuleAst selected;
    selected.imports = module.imports;
    selected.native_types = module.native_types;
    selected.native_classes = module.native_classes;
    selected.constants = {constant};
    CppEmitOptions fragment_options = options;
    fragment_options.emit_prelude = false;
    return emit_cpp_source(selected, fragment_options);
}

std::string emit_alias(const ModuleAst& module, const TypeAliasDecl& alias,
                       const CppEmitOptions& options) {
    return "using " + emitted_name(alias, options) + " = " +
           lower_cpp_type(alias.type_ref, namespace_aliases(module), options) + ";\n";
}

struct GeneratedCpp {
    std::string source;
    std::string content;
};

enum class DeclarationKind {
    Function,
    ClassMethod,
    EnumMethod,
    Class,
    Enum,
};

struct DeclarationSelection {
    DeclarationKind kind = DeclarationKind::Function;
    const FunctionDecl* function = nullptr;
    const ClassDecl* klass = nullptr;
    const EnumDecl* en = nullptr;
    SourceRange range;
};

bool better_declaration_range(const SourceRange& candidate, const SourceRange& current) {
    if (!valid_range(current)) {
        return true;
    }
    const SourcePosition candidate_start(candidate.start);
    const SourcePosition current_start(current.start);
    if (position_less(current_start, candidate_start)) {
        return true;
    }
    if (position_less(candidate_start, current_start)) {
        return false;
    }
    return position_less(candidate.end, current.end);
}

void consider_declaration(DeclarationSelection& selected, const RequestedRange& request,
                          DeclarationKind kind, const SourceRange& range,
                          const FunctionDecl* function = nullptr,
                          const ClassDecl* klass = nullptr, const EnumDecl* en = nullptr) {
    if (!request_starts_in(request, range) || !better_declaration_range(range, selected.range)) {
        return;
    }
    selected = {.kind = kind,
                .function = function,
                .klass = klass,
                .en = en,
                .range = range};
}

std::optional<GeneratedCpp> declaration_selection(const ModuleAst& module,
                                                  const RequestedRange& request,
                                                  const CppEmitOptions& options) {
    DeclarationSelection selected;
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            consider_declaration(selected, request, DeclarationKind::ClassMethod, method.range,
                                 &method, &klass);
        }
    }
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            consider_declaration(selected, request, DeclarationKind::EnumMethod, method.range,
                                 &method, nullptr, &en);
        }
    }
    for (const FunctionDecl& function : module.functions) {
        consider_declaration(selected, request, DeclarationKind::Function, function.range,
                             &function);
    }
    for (const ClassDecl& klass : module.classes) {
        consider_declaration(selected, request, DeclarationKind::Class, klass.range, nullptr,
                             &klass);
    }
    for (const EnumDecl& en : module.enums) {
        consider_declaration(selected, request, DeclarationKind::Enum, en.range, nullptr, nullptr,
                             &en);
    }
    if (valid_range(selected.range)) {
        switch (selected.kind) {
        case DeclarationKind::Function:
            return GeneratedCpp{selected.function->name,
                                emit_function(module, *selected.function, options)};
        case DeclarationKind::ClassMethod:
            return GeneratedCpp{selected.klass->name + "." + selected.function->name,
                                emit_class(module, *selected.klass, selected.function, options)};
        case DeclarationKind::EnumMethod:
            return GeneratedCpp{selected.en->name + "." + selected.function->name,
                                emit_enum(module, *selected.en, selected.function, options)};
        case DeclarationKind::Class:
            return GeneratedCpp{selected.klass->name,
                                emit_class(module, *selected.klass, nullptr, options)};
        case DeclarationKind::Enum:
            return GeneratedCpp{selected.en->name,
                                emit_enum(module, *selected.en, nullptr, options)};
        }
    }
    for (const ConstDecl& constant : module.constants) {
        if (constant.location.line == request.source.start.line) {
            return GeneratedCpp{constant.name, emit_constant(module, constant, options)};
        }
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        if (alias.location.line == request.source.start.line) {
            return GeneratedCpp{alias.name, emit_alias(module, alias, options)};
        }
    }
    return std::nullopt;
}

std::string result_json(const GeneratedCpp& generated) {
    return "{\"language\":\"cpp\",\"source\":\"" + json_escape(generated.source) +
           "\",\"content\":\"" + json_escape(generated.content) + "\"}";
}

} // namespace

std::string generated_cpp_json(const Document& document, const Json* command_argument,
                               const ProjectIndex& index) {
    const ModuleAst& module = index.visible_unit_for_path(document.path);
    const RequestedRange request = requested_range(document, command_argument);
    const CppModuleMap modules = module_map(index.merged_module());
    const CppEmitOptions options = make_cpp_module_emit_options(
        module, modules, false, preserve_public_abi_names(index.merged_module()));

    const LspPosition cursor{.line = request.source.start.line - 1,
                             .character = request.source.start.column - 1};
    const AstSelection cursor_selection = ast_selection_at(module, cursor);
    std::optional<Expr> selected_operator = cursor_selection.operator_expr;
    const ExpressionSelection expression =
        request.empty && selected_operator.has_value()
            ? expression_owner(module, *selected_operator)
            : expression_selection(module, request);
    if (expression.expression != nullptr &&
        (request.empty || request.source.start.line != expression.function->range.start.line ||
         request.source.start.column != expression.function->range.start.column)) {
        return result_json({expression_source_name(expression),
                            emit_expression(module, expression, options)});
    }
    if (const std::optional<GeneratedCpp> declaration =
            declaration_selection(module, request, options)) {
        return result_json(*declaration);
    }
    throw std::runtime_error("selection does not contain a Dudu declaration or expression");
}

} // namespace dudu
