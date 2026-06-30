#include "dudu/lsp/language_server_local_context.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace dudu {
namespace {

int target_line(const Json* params) {
    const Json* position = params == nullptr ? nullptr : params->get("position");
    return position == nullptr ? std::numeric_limits<int>::max()
                               : optional_int_value(position->get("line"));
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

bool location_matches_target_file(const SourceLocation& location,
                                  const SourceLocation& target_location) {
    if (target_location.file.empty()) {
        return true;
    }
    if (location.file.empty()) {
        return true;
    }
    const std::filesystem::path left(location.file.str());
    const std::filesystem::path right(target_location.file.str());
    return same_path(left, right) ||
           (!left.filename().empty() && left.filename() == right.filename());
}

bool function_contains_location(const FunctionDecl& fn, const SourceLocation& target_location) {
    return location_matches_target_file(fn.location, target_location) &&
           function_contains_line(fn, target_location.line);
}

bool token_is_syntax(const Token& token) {
    return token.kind != TokenKind::Newline && token.kind != TokenKind::Indent &&
           token.kind != TokenKind::Dedent && token.kind != TokenKind::End;
}

int token_start_character(const Token& token) {
    return std::max(0, token.location.column - 1);
}

int token_end_character(const Token& token) {
    return token_start_character(token) + static_cast<int>(token.text.size());
}

bool token_before_or_at_cursor(const Token& token, int line, int character) {
    const int token_line = token.location.line - 1;
    if (token_line != line) {
        return false;
    }
    return token_start_character(token) < character;
}

std::vector<Token> syntax_tokens_before_cursor(const Document& doc, int line, int character) {
    std::vector<Token> out;
    for (const Token& token : lex_source(doc.text, doc.path)) {
        if (!token_is_syntax(token)) {
            continue;
        }
        if (!token_before_or_at_cursor(token, line, character)) {
            continue;
        }
        out.push_back(token);
    }
    return out;
}

std::optional<size_t> member_access_dot_index(const std::vector<Token>& tokens, int character) {
    if (tokens.empty()) {
        return std::nullopt;
    }
    const Token& last = tokens.back();
    if (last.kind == TokenKind::Dot && token_end_character(last) <= character) {
        return tokens.size() - 1;
    }
    if (last.kind == TokenKind::Identifier && tokens.size() >= 2 &&
        tokens[tokens.size() - 2].kind == TokenKind::Dot &&
        token_start_character(last) <= character) {
        return tokens.size() - 2;
    }
    return std::nullopt;
}

std::optional<std::string> dotted_target_before(const std::vector<Token>& tokens,
                                                size_t dot_index) {
    if (dot_index == 0 || tokens[dot_index - 1].kind != TokenKind::Identifier) {
        return std::nullopt;
    }
    size_t start = dot_index - 1;
    while (start >= 2 && tokens[start - 1].kind == TokenKind::Dot &&
           tokens[start - 2].kind == TokenKind::Identifier) {
        start -= 2;
    }
    std::string out;
    for (size_t i = start; i < dot_index; ++i) {
        if (tokens[i].kind != TokenKind::Identifier && tokens[i].kind != TokenKind::Dot) {
            return std::nullopt;
        }
        out += tokens[i].text;
    }
    return out.empty() ? std::nullopt : std::optional<std::string>{out};
}

void lsp_bind_local(FunctionScope& scope, const std::string& name, TypeRef type_ref) {
    if (name.empty()) {
        return;
    }
    if (!has_type_ref(type_ref)) {
        type_ref = named_type_ref("auto");
    }
    bind_local(scope, name, type_ref);
}

TypeRef infer_lsp_expr_type(FunctionScope& scope, const Expr& expr) {
    return infer_expr_type_ast(scope, expr, &diagnostic_location(expr.location, expr));
}

void lsp_bind_inferred_local(FunctionScope& scope, const std::string& name, const Expr& expr) {
    const TypeRef inferred = infer_lsp_expr_type(scope, expr);
    lsp_bind_local(scope, name, inferred);
}

void bind_tuple_names(FunctionScope& scope, const Stmt& stmt) {
    const std::vector<std::string> names = tuple_binding_names(stmt_target_expr(stmt));
    if (names.empty()) {
        return;
    }
    const std::vector<TypeRef> types = template_type_arg_refs_with_aliases(
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
        if (has_stmt_type_ref(stmt)) {
            const TypeRef& declared_type = stmt_type_ref(stmt);
            const ArrayShapeInference inferred =
                infer_array_literal_shape_type(declared_type, stmt.value_expr);
            const TypeRef type_ref =
                inferred.status == ArrayShapeStatus::Inferred ? inferred.type_ref : declared_type;
            lsp_bind_local(scope, stmt.name, type_ref);
            return;
        }
        lsp_bind_inferred_local(scope, stmt.name, stmt.value_expr);
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (!tuple_binding_names(stmt_target_expr(stmt)).empty()) {
            bind_tuple_names(scope, stmt);
            return;
        }
        if (stmt_target_expr(stmt).kind == ExprKind::Name &&
            !scope.local_type_refs.contains(stmt_target_expr(stmt).name)) {
            lsp_bind_inferred_local(scope, stmt_target_expr(stmt).name, stmt.value_expr);
        }
    }
    if (stmt.kind == StmtKind::Except && !stmt.name.empty()) {
        lsp_bind_local(scope, stmt.name, stmt_type_ref(stmt));
    }
}

std::optional<TypeRef> infer_lsp_for_binding_type(FunctionScope& scope, const Stmt& stmt) {
    if (!has_stmt_iterable_expr(stmt)) {
        return std::nullopt;
    }
    if (direct_callee_name(stmt_iterable_expr(stmt)) == "range") {
        return named_type_ref("i32", stmt_iterable_expr(stmt).location);
    }
    if (stmt_iterable_expr(stmt).kind == ExprKind::Name) {
        const TypeRef local_ref =
            local_type_ref(scope, stmt_iterable_expr(stmt).name, stmt_iterable_expr(stmt).location);
        if (const auto element = iterable_type_ref_from_type(local_ref)) {
            return *element;
        }
    }
    const TypeRef iterable_type = infer_lsp_expr_type(scope, stmt_iterable_expr(stmt));
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
        TypeRef binding_type = stmt_type_ref(stmt);
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
        if (param.name == "self" && !scope.current_class.empty() && !has_type_ref(param.type_ref)) {
            lsp_bind_local(scope, param.name, named_type_ref(scope.current_class, param.location));
            continue;
        }
        lsp_bind_local(scope, param.name, param.type_ref);
    }
    collect_block_locals(scope, fn.statements, cursor_line);
}

} // namespace

std::optional<std::string> member_completion_target(const Document& doc, const Json* params) {
    const LspPosition position = lsp_position(params);
    const std::vector<Token> tokens =
        syntax_tokens_before_cursor(doc, position.line, position.character);
    const std::optional<size_t> dot_index = member_access_dot_index(tokens, position.character);
    if (!dot_index) {
        return std::nullopt;
    }
    return dotted_target_before(tokens, *dot_index);
}

TypeRef local_type_ref_before_cursor(const ModuleAst& module, const std::string& name,
                                     const Json* params) {
    const std::map<std::string, TypeRef> locals = local_type_refs_before_cursor(module, params);
    const auto found = locals.find(name);
    return found == locals.end() ? TypeRef{} : found->second;
}

TypeRef local_type_ref_before_cursor(const ModuleAst& module, const Document& doc,
                                     const std::string& name, const Json* params) {
    const std::map<std::string, TypeRef> locals =
        local_type_refs_before_cursor(module, doc, params);
    const auto found = locals.find(name);
    return found == locals.end() ? TypeRef{} : found->second;
}

std::map<std::string, TypeRef> local_type_refs_before_cursor(const ModuleAst& module,
                                                             const Json* params) {
    return local_type_refs_before_line(module, one_based_cursor_line(params));
}

std::map<std::string, TypeRef>
local_type_refs_before_cursor(const ModuleAst& module, const Document& doc, const Json* params) {
    SourceLocation location;
    location.file = SourceFileName(doc.path.string());
    location.line = one_based_cursor_line(params);
    return local_type_refs_before_location(module, location);
}

std::map<std::string, TypeRef> local_type_refs_before_line(const ModuleAst& module,
                                                           int one_based_line) {
    SourceLocation location;
    location.line = one_based_line;
    return local_type_refs_before_location(module, location);
}

std::map<std::string, TypeRef> local_type_refs_before_location(const ModuleAst& module,
                                                               SourceLocation location) {
    try {
        Symbols symbols = collect_symbols(module);
        FunctionScope scope(symbols);
        for (const FunctionDecl& fn : module.functions) {
            if (!function_contains_location(fn, location)) {
                continue;
            }
            collect_function_locals(scope, fn, location.line);
            return scope.local_type_refs;
        }
        for (const ClassDecl& klass : module.classes) {
            for (const FunctionDecl& method : klass.methods) {
                if (!function_contains_location(method, location)) {
                    continue;
                }
                Symbols method_symbols = with_self_type(symbols, klass.name);
                FunctionScope method_scope(method_symbols);
                method_scope.current_class = klass.name;
                collect_function_locals(method_scope, method, location.line);
                scope.local_type_refs = std::move(method_scope.local_type_refs);
                return scope.local_type_refs;
            }
        }
    } catch (const std::exception&) {
    }
    return {};
}

std::set<std::string> type_candidate_names(const TypeRef& type) {
    std::set<std::string> out;
    if (!has_type_ref(type)) {
        return out;
    }
    if (const std::string head = type_ref_head_name(type); !head.empty()) {
        out.insert(head);
        for (std::string_view tag : {"struct ", "class ", "union ", "enum "}) {
            if (head.starts_with(tag)) {
                out.insert(trim_copy(head.substr(tag.size())));
            }
        }
    }
    return out;
}

std::set<std::string> member_candidate_types(const ModuleAst& module, const TypeRef& type) {
    std::set<std::string> out = type_candidate_names(type);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const NativeTypeDecl& alias : module.native_types) {
            if (!out.contains(alias.name)) {
                continue;
            }
            for (const std::string& alias_type : type_candidate_names(alias.type_ref)) {
                if (out.insert(alias_type).second) {
                    changed = true;
                }
            }
        }
        for (const TypeAliasDecl& alias : module.aliases) {
            if (!out.contains(alias.name)) {
                continue;
            }
            for (const std::string& alias_type : type_candidate_names(alias.type_ref)) {
                if (out.insert(alias_type).second) {
                    changed = true;
                }
            }
        }
    }
    return out;
}

} // namespace dudu
