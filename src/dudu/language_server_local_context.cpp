#include "dudu/language_server_local_context.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_expr.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_scope.hpp"

#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>

namespace dudu {
namespace {

int target_line(const Json* params) {
    const Json* position = params == nullptr ? nullptr : params->get("position");
    return position == nullptr ? std::numeric_limits<int>::max() : int_value(position->get("line"));
}

int one_based_cursor_line(const Json* params) {
    const int line = target_line(params);
    return line == std::numeric_limits<int>::max() ? line : line + 1;
}

bool location_before_or_at(const SourceLocation& location, int cursor_line) {
    return cursor_line == std::numeric_limits<int>::max() || location.line <= cursor_line;
}

bool range_contains_line(const SourceRange& range, int cursor_line) {
    if (cursor_line == std::numeric_limits<int>::max()) {
        return true;
    }
    const int start = range.start.line;
    const int end = range.end.line <= 0 ? start : range.end.line;
    return start <= cursor_line && cursor_line <= end;
}

bool function_contains_line(const FunctionDecl& fn, int cursor_line) {
    if (cursor_line == std::numeric_limits<int>::max()) {
        return true;
    }
    int end = fn.location.line;
    if (!fn.statements.empty()) {
        end = fn.statements.back().range.end.line > 0 ? fn.statements.back().range.end.line
                                                      : fn.statements.back().location.line;
    }
    return fn.location.line <= cursor_line && cursor_line <= std::max(fn.location.line, end);
}

void lsp_bind_local(FunctionScope& scope, const std::string& name, TypeRef type_ref) {
    if (name.empty()) {
        return;
    }
    if (!has_type_ref(type_ref)) {
        type_ref = parse_type_text("auto");
    }
    bind_local(scope, name, type_ref);
}

TypeRef infer_lsp_expr_type(FunctionScope& scope, const Expr& expr) {
    BodyCheckCallbacks callbacks = expression_body_check_callbacks();
    return callbacks.infer_expr_type(scope, expr, &node_location(expr.location, expr));
}

void lsp_bind_inferred_local(FunctionScope& scope, const std::string& name, const Expr& expr) {
    const TypeRef inferred = infer_lsp_expr_type(scope, expr);
    lsp_bind_local(scope, name, inferred);
}

void bind_tuple_names(FunctionScope& scope, const Stmt& stmt) {
    const std::vector<std::string> names = tuple_binding_names(stmt.target_expr);
    if (names.empty()) {
        return;
    }
    const std::vector<TypeRef> types = template_type_arg_refs_resolved(
        infer_lsp_expr_type(scope, stmt.value_expr), "tuple", scope.symbols.alias_type_refs);
    if (names.size() != types.size()) {
        return;
    }
    for (size_t i = 0; i < names.size(); ++i) {
        lsp_bind_local(scope, names[i], types[i]);
    }
}

void bind_statement(FunctionScope& scope, const Stmt& stmt) {
    if (stmt.kind == StmtKind::VarDecl) {
        if (has_type_ref(stmt.type_ref)) {
            const ArrayShapeInference inferred =
                infer_array_literal_shape_type(stmt.type_ref, stmt.value_expr);
            const TypeRef type_ref =
                inferred.status == ArrayShapeStatus::Inferred ? inferred.type_ref : stmt.type_ref;
            lsp_bind_local(scope, stmt.name, type_ref);
            return;
        }
        lsp_bind_inferred_local(scope, stmt.name, stmt.value_expr);
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (!tuple_binding_names(stmt.target_expr).empty()) {
            bind_tuple_names(scope, stmt);
            return;
        }
        if (stmt.target_expr.kind == ExprKind::Name &&
            !scope.local_type_refs.contains(stmt.target_expr.name)) {
            lsp_bind_inferred_local(scope, stmt.target_expr.name, stmt.value_expr);
        }
    }
    if (stmt.kind == StmtKind::Except && !stmt.name.empty()) {
        lsp_bind_local(scope, stmt.name, stmt.type_ref);
    }
}

std::optional<TypeRef> infer_lsp_for_binding_type(FunctionScope& scope, const Stmt& stmt) {
    if (!sema_has_expr(stmt.iterable_expr)) {
        return std::nullopt;
    }
    if (direct_callee_name(stmt.iterable_expr) == "range") {
        return parse_type_text("i32", stmt.iterable_expr.location);
    }
    if (stmt.iterable_expr.kind == ExprKind::Name) {
        const TypeRef local_ref =
            local_type_ref(scope, stmt.iterable_expr.name, stmt.iterable_expr.location);
        if (const auto element = iterable_type_ref_from_type(local_ref)) {
            return *element;
        }
    }
    const TypeRef iterable_type = infer_lsp_expr_type(scope, stmt.iterable_expr);
    if (const auto element = iterable_type_ref_from_type(iterable_type)) {
        return *element;
    }
    return std::nullopt;
}

void collect_block_locals(FunctionScope& scope, const std::vector<Stmt>& statements,
                          int cursor_line);

void collect_for_body_locals(FunctionScope scope, const Stmt& stmt, int cursor_line,
                             std::map<std::string, TypeRef>& out) {
    if (!stmt.name.empty()) {
        TypeRef binding_type = stmt.type_ref;
        if (!has_type_ref(binding_type)) {
            if (const auto inferred = infer_lsp_for_binding_type(scope, stmt)) {
                binding_type = *inferred;
            }
        }
        lsp_bind_local(scope, stmt.name, binding_type);
    }
    collect_block_locals(scope, stmt.children, cursor_line);
    out = scope.local_type_refs;
}

void collect_block_locals(FunctionScope& scope, const std::vector<Stmt>& statements,
                          int cursor_line) {
    for (const Stmt& stmt : statements) {
        if (!location_before_or_at(stmt.location, cursor_line)) {
            continue;
        }
        bind_statement(scope, stmt);
        if (!range_contains_line(stmt.range, cursor_line)) {
            continue;
        }
        if (stmt.kind == StmtKind::For) {
            std::map<std::string, TypeRef> nested;
            collect_for_body_locals(scope, stmt, cursor_line, nested);
            if (cursor_line != std::numeric_limits<int>::max()) {
                scope.local_type_refs = std::move(nested);
            }
            continue;
        }
        if (!stmt.children.empty()) {
            collect_block_locals(scope, stmt.children, cursor_line);
        }
    }
}

void collect_function_locals(FunctionScope& scope, const FunctionDecl& fn, int cursor_line) {
    for (const ParamDecl& param : fn.params) {
        lsp_bind_local(scope, param.name, param.type_ref);
    }
    collect_block_locals(scope, fn.statements, cursor_line);
}

std::optional<FunctionScope> local_scope_before_cursor(const Document& doc, const Json* params) {
    const int cursor_line = one_based_cursor_line(params);
    ModuleAst module = parse_source(doc.text, doc.path);
    Symbols symbols = collect_symbols(module);
    FunctionScope scope(symbols);
    for (const FunctionDecl& fn : module.functions) {
        if (!function_contains_line(fn, cursor_line)) {
            continue;
        }
        collect_function_locals(scope, fn, cursor_line);
        return scope;
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (!function_contains_line(method, cursor_line)) {
                continue;
            }
            scope.current_class = klass.name;
            collect_function_locals(scope, method, cursor_line);
            return scope;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> member_completion_target(const Document& doc, const Json* params) {
    const Json* position = params == nullptr ? nullptr : params->get("position");
    const int target_line = int_value(position == nullptr ? nullptr : position->get("line"));
    const int target_character =
        int_value(position == nullptr ? nullptr : position->get("character"));
    std::istringstream in(doc.text);
    std::string line;
    for (int row = 0; std::getline(in, line); ++row) {
        if (row != target_line) {
            continue;
        }
        int cursor = std::min(target_character, static_cast<int>(line.size()));
        while (cursor > 0 && identifier_char(line[static_cast<size_t>(cursor - 1)])) {
            --cursor;
        }
        if (cursor <= 0 || line[static_cast<size_t>(cursor - 1)] != '.') {
            return std::nullopt;
        }
        int end = cursor - 1;
        while (end > 0 &&
               std::isspace(static_cast<unsigned char>(line[static_cast<size_t>(end - 1)])) != 0) {
            --end;
        }
        int start = end;
        while (start > 0 && (identifier_char(line[static_cast<size_t>(start - 1)]) ||
                             line[static_cast<size_t>(start - 1)] == '.')) {
            --start;
        }
        if (start == end) {
            return std::nullopt;
        }
        return line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
    }
    return std::nullopt;
}

TypeRef local_type_ref_before_cursor(const Document& doc, const std::string& name,
                                     const Json* params) {
    const std::map<std::string, TypeRef> locals = local_type_refs_before_cursor(doc, params);
    const auto found = locals.find(name);
    return found == locals.end() ? TypeRef{} : found->second;
}

std::map<std::string, TypeRef> local_type_refs_before_cursor(const Document& doc,
                                                             const Json* params) {
    try {
        if (const std::optional<FunctionScope> scope = local_scope_before_cursor(doc, params)) {
            return scope->local_type_refs;
        }
    } catch (const std::exception&) {
    }
    return {};
}

std::set<std::string> member_candidate_types(const ModuleAst& module, const std::string& type) {
    std::set<std::string> out{type};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const NativeTypeDecl& alias : module.native_types) {
            const std::string alias_type =
                has_type_ref(alias.type_ref) ? type_ref_text(alias.type_ref) : alias.type;
            if (!alias_type.empty() && out.contains(alias.name) && out.insert(alias_type).second) {
                changed = true;
            }
        }
        for (const TypeAliasDecl& alias : module.aliases) {
            const std::string alias_type = type_ref_text(alias.type_ref);
            if (!alias_type.empty() && out.contains(alias.name) && out.insert(alias_type).second) {
                changed = true;
            }
        }
    }
    return out;
}

} // namespace dudu
