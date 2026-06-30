#include "dudu/lsp/language_server_inlay_hints.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <algorithm>
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
    int kind = 1;
};

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

std::vector<std::string> safe_lines(const Document& doc) {
    return document_lines(doc.text);
}

bool source_has_explicit_type_after_name(const Document& doc, SourceLocation location,
                                         const std::string& name) {
    const std::vector<std::string> lines = safe_lines(doc);
    const int line = location.line - 1;
    if (line < 0 || static_cast<size_t>(line) >= lines.size()) {
        return false;
    }
    const std::string& text = lines[static_cast<size_t>(line)];
    size_t cursor = static_cast<size_t>(std::max(0, location.column - 1)) + name.size();
    while (cursor < text.size() && text[cursor] == ' ') {
        ++cursor;
    }
    return cursor < text.size() && text[cursor] == ':';
}

void add_type_hint(std::vector<InlayHint>& hints, SourceLocation location, const std::string& name,
                   const TypeRef& type) {
    const std::string label = type_label(type);
    if (name.empty() || label.empty()) {
        return;
    }
    hints.push_back({.line = hint_line(location),
                     .character = hint_character_after(location, name),
                     .label = ": " + label,
                     .kind = 1});
}

void bind_inlay_local(FunctionScope& scope, const std::string& name, TypeRef type) {
    if (name.empty()) {
        return;
    }
    if (!hintable_type(type)) {
        type = named_type_ref("auto");
    }
    scope.local_type_refs[name] = std::move(type);
}

TypeRef infer_type(FunctionScope& scope, const Expr& expr) {
    try {
        return infer_expr_type_ast(scope, expr, &expr.location);
    } catch (const std::exception&) {
        return {};
    }
}

std::optional<TypeRef> infer_for_binding_type(FunctionScope& scope, const Stmt& stmt) {
    if (stmt.iterable_expr == nullptr) {
        return std::nullopt;
    }
    const Expr& iterable = *stmt.iterable_expr;
    if (iterable.kind == ExprKind::Call && iterable.callee != nullptr &&
        !iterable.callee->empty() && iterable.callee->front().kind == ExprKind::Name &&
        iterable.callee->front().name == "range") {
        return named_type_ref("i32", iterable.location);
    }
    const TypeRef iterable_type = infer_type(scope, iterable);
    const std::vector<TypeRef> list_args =
        template_type_arg_refs_with_aliases(iterable_type, "list", scope.symbols.alias_type_refs);
    if (!list_args.empty()) {
        return list_args.front();
    }
    const std::vector<TypeRef> vector_args = template_type_arg_refs_with_aliases(
        iterable_type, "std.vector", scope.symbols.alias_type_refs);
    if (!vector_args.empty()) {
        return vector_args.front();
    }
    return std::nullopt;
}

void collect_hints_for_block(const Document& doc, FunctionScope& scope,
                             const std::vector<Stmt>& statements, std::vector<InlayHint>& hints);

void bind_tuple_names(FunctionScope& scope, const Stmt& stmt) {
    if (stmt.target_expr == nullptr) {
        return;
    }
    const std::vector<std::string> names = tuple_binding_names(*stmt.target_expr);
    if (names.empty()) {
        return;
    }
    const std::vector<TypeRef> types = template_type_arg_refs_with_aliases(
        infer_type(scope, stmt.value_expr), "tuple", scope.symbols.alias_type_refs);
    if (names.size() != types.size()) {
        return;
    }
    for (size_t i = 0; i < names.size(); ++i) {
        bind_inlay_local(scope, names[i], types[i]);
    }
}

void collect_hint_for_statement(const Document& doc, FunctionScope& scope, const Stmt& stmt,
                                std::vector<InlayHint>& hints) {
    if (stmt.kind == StmtKind::VarDecl) {
        TypeRef type =
            has_stmt_type_ref(stmt) ? stmt_type_ref(stmt) : infer_type(scope, stmt.value_expr);
        if (!source_has_explicit_type_after_name(doc, stmt.location, stmt.name)) {
            add_type_hint(hints, stmt.location, stmt.name, type);
        }
        bind_inlay_local(scope, stmt.name, std::move(type));
        return;
    }

    if (stmt.kind == StmtKind::Assign && stmt.target_expr != nullptr) {
        const Expr& target = *stmt.target_expr;
        if (!tuple_binding_names(target).empty()) {
            bind_tuple_names(scope, stmt);
            return;
        }
        if (target.kind == ExprKind::Name && !scope.local_type_refs.contains(target.name.str())) {
            TypeRef type = infer_type(scope, stmt.value_expr);
            add_type_hint(hints, target.location, target.name, type);
            bind_inlay_local(scope, target.name.str(), std::move(type));
        }
        return;
    }

    if (stmt.kind == StmtKind::For) {
        FunctionScope body_scope = scope;
        TypeRef binding_type = has_stmt_type_ref(stmt) ? stmt_type_ref(stmt) : TypeRef{};
        if (!hintable_type(binding_type)) {
            if (const std::optional<TypeRef> inferred = infer_for_binding_type(scope, stmt)) {
                binding_type = *inferred;
            }
        }
        if (!source_has_explicit_type_after_name(doc, stmt.location, stmt.name)) {
            add_type_hint(hints, stmt.location, stmt.name, binding_type);
        }
        bind_inlay_local(body_scope, stmt.name, std::move(binding_type));
        collect_hints_for_block(doc, body_scope, stmt.children, hints);
        return;
    }

    if (stmt.kind == StmtKind::Except && !stmt.name.empty()) {
        bind_inlay_local(scope, stmt.name, stmt_type_ref(stmt));
    }

    if (!stmt.children.empty()) {
        FunctionScope child_scope = scope;
        collect_hints_for_block(doc, child_scope, stmt.children, hints);
    }
}

void collect_hints_for_block(const Document& doc, FunctionScope& scope,
                             const std::vector<Stmt>& statements, std::vector<InlayHint>& hints) {
    for (const Stmt& stmt : statements) {
        collect_hint_for_statement(doc, scope, stmt, hints);
    }
}

void collect_hints_for_function(const Document& doc, const Symbols& symbols, const FunctionDecl& fn,
                                std::string current_class, std::vector<InlayHint>& hints) {
    FunctionScope scope(symbols);
    scope.current_class = std::move(current_class);
    for (const ParamDecl& param : fn.params) {
        if (param.name == "self" &&
            !source_has_explicit_type_after_name(doc, param.location, param.name)) {
            add_type_hint(hints, param.location, param.name, param.type_ref);
        }
        bind_inlay_local(scope, param.name, param.type_ref);
    }
    collect_hints_for_block(doc, scope, fn.statements, hints);
}

std::string hint_json(const InlayHint& hint) {
    std::ostringstream out;
    out << "{\"position\":{\"line\":" << hint.line << ",\"character\":" << hint.character
        << "},\"label\":\"" << json_escape(hint.label) << "\",\"kind\":" << hint.kind
        << ",\"paddingLeft\":true}";
    return out.str();
}

} // namespace

std::string inlay_hints_json(const Document& doc, const Json*) {
    std::vector<InlayHint> hints;
    try {
        const ProjectIndex& index = project_index_for_document(doc, false, false);
        const ModuleAst& module = index.visible_unit_for_path(doc.path);
        const Symbols symbols = collect_symbols(module);
        for (const FunctionDecl& fn : module.functions) {
            collect_hints_for_function(doc, symbols, fn, {}, hints);
        }
        for (const ClassDecl& klass : module.classes) {
            const Symbols method_symbols = with_self_type(symbols, klass.name);
            for (const FunctionDecl& method : klass.methods) {
                collect_hints_for_function(doc, method_symbols, method, klass.name, hints);
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
