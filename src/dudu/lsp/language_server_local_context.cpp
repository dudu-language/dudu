#include "dudu/lsp/language_server_local_context.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_scope.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"
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

void collect_block_locals(FunctionScope& scope, const std::vector<Stmt>& statements,
                          int cursor_line);

void collect_for_body_locals(FunctionScope scope, const Stmt& stmt, int cursor_line,
                             std::map<std::string, TypeRef>& out) {
    if (!stmt.name.empty()) {
        TypeRef binding_type = stmt_type_ref(stmt);
        if (!has_type_ref(binding_type)) {
            if (const auto inferred = infer_lsp_loop_binding_type(scope, stmt)) {
                binding_type = *inferred;
            }
        }
        bind_lsp_local(scope, stmt.name, binding_type);
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
        bind_lsp_statement(scope, stmt);
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
    bind_lsp_function_params(scope, fn);
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
            const Symbols function_symbols = symbols_for_lsp_function(symbols, fn);
            FunctionScope fn_scope(function_symbols);
            collect_function_locals(fn_scope, fn, location.line);
            scope.local_type_refs = std::move(fn_scope.local_type_refs);
            return scope.local_type_refs;
        }
        for (const ClassDecl& klass : module.classes) {
            for (const FunctionDecl& method : klass.methods) {
                if (!function_contains_location(method, location)) {
                    continue;
                }
                Symbols method_symbols = with_self_type(symbols, klass.name);
                if (!klass.generic_params.empty()) {
                    method_symbols =
                        with_generic_params(std::move(method_symbols), klass.generic_params,
                                            generic_value_params_for_class(klass));
                }
                method_symbols = symbols_for_lsp_function(std::move(method_symbols), method);
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
    if (const auto child = unary_type_child_ref(
            type, {TypeKind::Pointer, TypeKind::Reference, TypeKind::Const, TypeKind::Volatile,
                   TypeKind::Atomic, TypeKind::Storage, TypeKind::Shared, TypeKind::Device})) {
        out.merge(type_candidate_names(*child));
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
