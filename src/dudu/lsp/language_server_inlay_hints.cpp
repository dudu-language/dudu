#include "dudu/lsp/language_server_inlay_hints.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_inlay_type_details.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_presentation_symbols.hpp"
#include "dudu/lsp/language_server_scope.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_constructors.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

struct InlayHint {
    int line = 0;
    int character = 0;
    std::string label;
    std::string label_json;
    std::string tooltip_json;
    int kind = 1;
};

struct ParamHints {
    std::vector<std::string> names;
    std::vector<TypeRef> types;
};

bool placeholder_param_name(std::string_view name) {
    if (!name.starts_with("arg") || name.size() == 3) {
        return false;
    }
    return std::all_of(name.begin() + 3, name.end(),
                       [](const char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
}

bool hintable_param_name(const std::string& name) {
    return !name.empty() && name != "self" && !placeholder_param_name(name);
}

bool hintable_type(const TypeRef& type) {
    return has_type_ref(type) && !type_ref_is_auto(type);
}

std::string type_label(const TypeRef& type) {
    if (!hintable_type(type)) {
        return {};
    }
    try {
        return type_ref_text(type);
    } catch (const std::exception&) {
        return {};
    }
}

int hint_line(SourceLocation location) {
    return std::max(0, location.line - 1);
}

int hint_character_after(SourceLocation location, const std::string& name) {
    return std::max(0, location.column - 1 + static_cast<int>(name.size()));
}

void add_type_hint(std::vector<InlayHint>& hints, const Document& doc, const Symbols& symbols,
                   SourceLocation location, const std::string& name, const TypeRef& type) {
    const std::string label = type_label(type);
    if (name.empty() || label.empty()) {
        return;
    }
    const InlayTypeDetail detail = inlay_type_detail(doc, symbols, type, ": ");
    hints.push_back({.line = hint_line(location),
                     .character = hint_character_after(location, name),
                     .label = detail.label,
                     .label_json = inlay_label_json(detail, doc),
                     .tooltip_json = inlay_tooltip_json(detail),
                     .kind = 1});
}

void add_inferred_array_shape_hint(std::vector<InlayHint>& hints, const Document& doc,
                                   const Symbols& symbols, const TypeRef& declared,
                                   const TypeRef& inferred) {
    if (declared.kind != TypeKind::Template || declared.name != "array" ||
        declared.range.end.line <= 0 || declared.range.end.column <= 0) {
        return;
    }
    const std::vector<std::string> extents = explicit_array_shape_values(inferred);
    if (extents.empty()) {
        return;
    }
    std::ostringstream label;
    label << '[';
    for (size_t i = 0; i < extents.size(); ++i) {
        if (i > 0) {
            label << ", ";
        }
        label << extents[i];
    }
    label << ']';
    const InlayTypeDetail detail = inlay_type_detail(doc, symbols, inferred, "");
    hints.push_back({.line = declared.range.end.line - 1,
                     .character = declared.range.end.column - 1,
                     .label = label.str(),
                     .label_json = {},
                     .tooltip_json = inlay_tooltip_json(detail),
                     .kind = 1});
}

void add_argument_type_hint(std::vector<InlayHint>& hints, const Document& doc,
                            const Symbols& symbols, const Expr& expr, const TypeRef& type) {
    const std::string label = type_label(type);
    if (label.empty() || expr.range.end.column <= 0) {
        return;
    }
    const InlayTypeDetail detail = inlay_type_detail(doc, symbols, type, ": ");
    hints.push_back({.line = std::max(0, expr.range.end.line - 1),
                     .character = std::max(0, expr.range.end.column - 1),
                     .label = detail.label,
                     .label_json = inlay_label_json(detail, doc),
                     .tooltip_json = inlay_tooltip_json(detail),
                     .kind = 1});
}

std::string parameter_hint_tooltip_json(const Document& doc, const Symbols& symbols,
                                        const TypeRef& type) {
    if (!hintable_type(type)) {
        return {};
    }
    const InlayTypeDetail detail = inlay_type_detail(doc, symbols, type, "");
    return inlay_tooltip_json(detail);
}

void add_parameter_hint(std::vector<InlayHint>& hints, const Document& doc, const Symbols& symbols,
                        const Expr& expr, const std::string& name, const TypeRef& type) {
    if (name.empty() || expr.kind == ExprKind::NamedArg) {
        return;
    }
    hints.push_back({.line = hint_line(expr.location),
                     .character = std::max(0, expr.location.column - 1),
                     .label = name + ":",
                     .label_json = {},
                     .tooltip_json = parameter_hint_tooltip_json(doc, symbols, type),
                     .kind = 2});
}

TypeRef unwrap_ref_const(TypeRef type) {
    while ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const) &&
           type.children.size() == 1) {
        type = type.children.front();
    }
    return type;
}

const ClassDecl* class_for_name(const Symbols& symbols, const std::string& name) {
    if (const auto found = symbols.classes.find(name); found != symbols.classes.end()) {
        return found->second;
    }
    return native_class_decl_for_binding(symbols, name);
}

const ClassDecl* class_for_type(const Symbols& symbols, const TypeRef& type) {
    const TypeRef unwrapped = unwrap_ref_const(resolve_alias_ref(symbols, type));
    return class_for_name(symbols, type_ref_head_name(unwrapped));
}

ParamHints constructor_param_hints(const ClassDecl& klass) {
    ParamHints hints;
    for (const ConstructorParam& param : constructor_params(klass)) {
        hints.names.push_back(hintable_param_name(param.name) ? param.name : "");
        hints.types.push_back(param.type_ref);
    }
    return hints;
}

ParamHints function_param_hints(const FunctionDecl& fn, bool skip_self) {
    ParamHints hints;
    size_t start = skip_self && !fn.params.empty() && fn.params.front().name == "self" ? 1 : 0;
    for (size_t i = start; i < fn.params.size(); ++i) {
        hints.names.push_back(hintable_param_name(fn.params[i].name) ? fn.params[i].name : "");
        hints.types.push_back(fn.params[i].type_ref);
    }
    return hints;
}

ParamHints native_function_param_hints(const NativeFunctionDecl& fn) {
    ParamHints hints;
    const std::vector<TypeRef> param_types = native_function_param_type_refs(fn);
    hints.names.reserve(fn.param_names.size());
    hints.types.reserve(fn.param_names.size());
    for (size_t i = 0; i < fn.param_names.size(); ++i) {
        const std::string& name = fn.param_names[i];
        hints.names.push_back(hintable_param_name(name) ? name : "");
        hints.types.push_back(i < param_types.size() ? param_types[i] : TypeRef{});
    }
    return hints;
}

ParamHints first_native_function_param_hints(const Symbols& symbols, const std::string& name,
                                             size_t arg_count) {
    const std::vector<const NativeFunctionDecl*> declarations =
        native_function_decls_for_binding(symbols, name);
    if (declarations.empty()) {
        return {};
    }
    for (const NativeFunctionDecl* fn : declarations) {
        if (fn == nullptr || fn->param_names.size() < arg_count) {
            continue;
        }
        ParamHints hints = native_function_param_hints(*fn);
        if (std::any_of(hints.names.begin(), hints.names.end(),
                        [](const std::string& item) { return !item.empty(); })) {
            return hints;
        }
    }
    return {};
}

ParamHints builtin_member_param_hints(const std::string& method_name) {
    if (method_name == "append" || method_name == "push") {
        return {.names = {"value"}, .types = {TypeRef{}}};
    }
    if (method_name == "insert") {
        return {.names = {"index", "value"}, .types = {TypeRef{}, TypeRef{}}};
    }
    return {};
}

ParamHints method_param_hints_for_class(const ClassDecl& klass, const std::string& method_name) {
    for (const FunctionDecl& method : klass.methods) {
        if (method.name == method_name) {
            return function_param_hints(method, true);
        }
    }
    return {};
}

ParamHints param_hints_for_call(FunctionScope& scope, const Expr& expr) {
    if (expr.kind != ExprKind::Call || !has_expr_callee(expr) || expr_callee(expr).empty()) {
        return {};
    }

    const Expr& callee_expr = expr_callee(expr).front();
    if (callee_expr.kind == ExprKind::Name) {
        const std::string callee = callee_expr.name;
        if (const auto found = scope.symbols.function_decls.find(callee);
            found != scope.symbols.function_decls.end()) {
            return function_param_hints(*found->second, false);
        }
        if (scope.local_type_refs.contains(callee)) {
            return {};
        }
        if (callee == "move" && expr.children.size() == 1) {
            return {.names = {"value"},
                    .types = {try_infer_lsp_expr_type(scope, expr.children.front())}};
        }
        if (ParamHints native =
                first_native_function_param_hints(scope.symbols, callee, expr.children.size());
            !native.names.empty()) {
            return native;
        }
        if (const ClassDecl* klass = class_for_name(scope.symbols, callee)) {
            return constructor_param_hints(*klass);
        }
        return {};
    }

    if (callee_expr.kind != ExprKind::Member || callee_expr.children.size() != 1) {
        return {};
    }

    const Expr& receiver = callee_expr.children.front();
    if (receiver.kind == ExprKind::Name) {
        if (receiver.name == "class" && !scope.current_class.empty()) {
            if (const ClassDecl* klass = class_for_name(scope.symbols, scope.current_class)) {
                return method_param_hints_for_class(*klass, callee_expr.name);
            }
        }
        if (!scope.local_type_refs.contains(receiver.name.str())) {
            if (const ClassDecl* klass = class_for_name(scope.symbols, receiver.name.str())) {
                return method_param_hints_for_class(*klass, callee_expr.name);
            }
        }
    }

    const TypeRef receiver_type = try_infer_lsp_expr_type(scope, receiver);
    if (const ClassDecl* klass = class_for_type(scope.symbols, receiver_type)) {
        return method_param_hints_for_class(*klass, callee_expr.name);
    }
    return builtin_member_param_hints(callee_expr.name);
}

void collect_hints_for_expr(const Document& doc, FunctionScope& scope, const Expr& expr,
                            const InlayHintOptions& options, std::vector<InlayHint>& hints) {
    if (expr.kind == ExprKind::Call) {
        const ParamHints params = param_hints_for_call(scope, expr);
        for (size_t i = 0; i < expr.children.size(); ++i) {
            const Expr& arg = expr.children[i];
            if (options.parameter_names && i < params.names.size()) {
                const TypeRef type = i < params.types.size() ? params.types[i] : TypeRef{};
                add_parameter_hint(hints, doc, scope.symbols, arg, params.names[i], type);
            }
            if (options.argument_types) {
                add_argument_type_hint(hints, doc, scope.symbols, arg,
                                       try_infer_lsp_expr_type(scope, arg));
            }
        }
    }

    if (expr.callee != nullptr) {
        for (const Expr& child : *expr.callee) {
            collect_hints_for_expr(doc, scope, child, options, hints);
        }
    }
    if (expr.template_args != nullptr) {
        for (const Expr& child : *expr.template_args) {
            collect_hints_for_expr(doc, scope, child, options, hints);
        }
    }
    for (const Expr& child : expr.children) {
        collect_hints_for_expr(doc, scope, child, options, hints);
    }
}

void collect_hints_for_statement_exprs(const Document& doc, FunctionScope& scope, const Stmt& stmt,
                                       const InlayHintOptions& options,
                                       std::vector<InlayHint>& hints) {
    auto collect = [&](const Expr& expr) {
        if (expr.kind != ExprKind::Missing) {
            collect_hints_for_expr(doc, scope, expr, options, hints);
        }
    };
    collect(stmt.expr);
    collect(stmt.value_expr);
    if (stmt.target_expr != nullptr) {
        collect(*stmt.target_expr);
    }
    if (stmt.condition_expr != nullptr) {
        collect(*stmt.condition_expr);
    }
    if (stmt.message_expr != nullptr) {
        collect(*stmt.message_expr);
    }
    if (stmt.iterable_expr != nullptr) {
        collect(*stmt.iterable_expr);
    }
    if (stmt.pattern_expr != nullptr) {
        collect(*stmt.pattern_expr);
    }
    if (stmt.guard_expr != nullptr) {
        collect(*stmt.guard_expr);
    }
}

void collect_hints_for_block(const Document& doc, FunctionScope& scope,
                             const std::vector<Stmt>& statements, const InlayHintOptions& options,
                             std::vector<InlayHint>& hints);

void collect_hint_for_statement(const Document& doc, FunctionScope& scope, const Stmt& stmt,
                                const InlayHintOptions& options, std::vector<InlayHint>& hints) {
    collect_hints_for_statement_exprs(doc, scope, stmt, options, hints);

    if (stmt.kind == StmtKind::VarDecl) {
        TypeRef type = has_stmt_type_ref(stmt) ? lsp_variable_type(stmt)
                                               : try_infer_lsp_expr_type(scope, stmt.value_expr);
        if (options.inferred_types && has_stmt_type_ref(stmt) &&
            type.kind == TypeKind::FixedArray) {
            add_inferred_array_shape_hint(hints, doc, scope.symbols, stmt_type_ref(stmt), type);
        }
        if (options.inferred_types && !has_stmt_type_ref(stmt)) {
            add_type_hint(hints, doc, scope.symbols, stmt.location, stmt.name, type);
        }
        bind_lsp_local(scope, stmt.name, std::move(type));
        return;
    }

    if (stmt.kind == StmtKind::Assign && stmt.target_expr != nullptr) {
        const Expr& target = *stmt.target_expr;
        if (!tuple_binding_names(target).empty()) {
            (void)try_bind_lsp_tuple_names(scope, stmt);
            return;
        }
        if (target.kind == ExprKind::Name && !scope.local_type_refs.contains(target.name.str())) {
            TypeRef type = try_infer_lsp_expr_type(scope, stmt.value_expr);
            if (options.inferred_types) {
                add_type_hint(hints, doc, scope.symbols, target.location, target.name, type);
            }
            bind_lsp_local(scope, target.name.str(), std::move(type));
        }
        return;
    }

    if (stmt.kind == StmtKind::For) {
        FunctionScope body_scope = scope;
        TypeRef binding_type = has_stmt_type_ref(stmt) ? stmt_type_ref(stmt) : TypeRef{};
        if (!hintable_type(binding_type)) {
            if (const std::optional<TypeRef> inferred = infer_lsp_loop_binding_type(scope, stmt)) {
                binding_type = *inferred;
            }
        }
        if (options.loop_binding_types && !has_stmt_type_ref(stmt)) {
            add_type_hint(hints, doc, scope.symbols, stmt.location, stmt.name, binding_type);
        }
        bind_lsp_local(body_scope, stmt.name, std::move(binding_type));
        collect_hints_for_block(doc, body_scope, stmt.children, options, hints);
        return;
    }

    if (stmt.kind == StmtKind::Except && !stmt.name.empty()) {
        bind_lsp_local(scope, stmt.name, stmt_type_ref(stmt));
    }

    if (!stmt.children.empty()) {
        FunctionScope child_scope = scope;
        collect_hints_for_block(doc, child_scope, stmt.children, options, hints);
    }
}

void collect_hints_for_block(const Document& doc, FunctionScope& scope,
                             const std::vector<Stmt>& statements, const InlayHintOptions& options,
                             std::vector<InlayHint>& hints) {
    for (const Stmt& stmt : statements) {
        collect_hint_for_statement(doc, scope, stmt, options, hints);
    }
}

void collect_hints_for_function(const Document& doc, const Symbols& symbols, const FunctionDecl& fn,
                                std::string current_class, const InlayHintOptions& options,
                                std::vector<InlayHint>& hints) {
    Symbols function_symbols = symbols_for_lsp_function(symbols, fn);
    FunctionScope scope(function_symbols);
    scope.current_class = std::move(current_class);
    for (const ParamDecl& param : fn.params) {
        const bool implicit_self_type = param.name == "self" &&
                                        param.type_ref.location.line == param.location.line &&
                                        param.type_ref.location.column == param.location.column;
        if (options.implicit_self && implicit_self_type) {
            add_type_hint(hints, doc, symbols, param.location, param.name, param.type_ref);
        }
    }
    bind_lsp_function_params(scope, fn);
    collect_hints_for_block(doc, scope, fn.statements, options, hints);
}

std::string hint_json(const InlayHint& hint) {
    std::ostringstream out;
    out << "{\"position\":{\"line\":" << hint.line << ",\"character\":" << hint.character
        << "},\"label\":";
    if (!hint.label_json.empty()) {
        out << hint.label_json;
    } else {
        out << "\"" << json_escape(hint.label) << "\"";
    }
    out << ",\"kind\":" << hint.kind << ",\"paddingLeft\":true";
    if (!hint.tooltip_json.empty()) {
        out << ",\"tooltip\":" << hint.tooltip_json;
    }
    out << "}";
    return out.str();
}

} // namespace

std::string inlay_hints_json(const Document& doc, const Json*, InlayHintOptions options) {
    std::vector<InlayHint> hints;
    try {
        const ProjectIndexSnapshot index = project_index_for_document(doc, true, false);
        const ModuleAst& module = index->visible_unit_for_path(doc.path);
        LspPresentationSymbols presentation = presentation_symbols(*index, module);
        Symbols& symbols = presentation.symbols;
        for (const FunctionDecl& fn : module.functions) {
            collect_hints_for_function(doc, symbols, fn, {}, options, hints);
        }
        for (const ClassDecl& klass : module.classes) {
            Symbols method_symbols = with_self_type(symbols, klass.name);
            if (!klass.generic_params.empty()) {
                method_symbols =
                    with_generic_params(std::move(method_symbols), klass.generic_params,
                                        generic_value_params_for_class(klass));
            }
            for (const FunctionDecl& method : klass.methods) {
                collect_hints_for_function(doc, method_symbols, method, klass.name, options, hints);
            }
        }
    } catch (const std::exception&) {
        return "[]";
    }

    std::sort(hints.begin(), hints.end(), [](const InlayHint& left, const InlayHint& right) {
        return std::tie(left.line, left.character, left.label) <
               std::tie(right.line, right.character, right.label);
    });

    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < hints.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << hint_json(hints[i]);
    }
    out << ']';
    return out.str();
}

} // namespace dudu
