#include "dudu/lsp/language_server_reference_support.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_signature_match.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

namespace dudu {

bool renameable_symbol_kind(const int kind) {
    return kind == lsp_symbol_kind::Class || kind == lsp_symbol_kind::Method ||
           kind == lsp_symbol_kind::Field || kind == lsp_symbol_kind::Enum ||
           kind == lsp_symbol_kind::Function || kind == lsp_symbol_kind::Constant;
}

namespace {

std::string symbol_range_key(const std::string& uri, const std::string& range) {
    return uri + "|" + range;
}

std::string symbol_range_key(const Symbol& symbol, const Document& doc) {
    const int line = std::max(0, symbol.location.line - 1);
    const int column = std::max(0, symbol.location.column - 1);
    return symbol_range_key(
        uri_for_location(symbol.location, doc),
        range_json(line, column, column + static_cast<int>(symbol.name.size())));
}

bool position_contains_name(const Json* params, const std::string& name,
                            const SourceLocation& location) {
    const LspPosition position = lsp_position(params);
    const int target_line = position.line + 1;
    const int target_column = position.character + 1;
    if (location.line != target_line || location.column <= 0) {
        return false;
    }
    const int start = location.column;
    const int end = start + static_cast<int>(name.size());
    return target_column >= start && target_column <= end;
}

} // namespace

std::optional<Symbol> declaration_at_position(const Document& doc, const Json* params,
                                              const std::string& name,
                                              const std::vector<Symbol>& symbols) {
    for (const Symbol& symbol : symbols) {
        if (symbol.name == name && renameable_symbol_kind(symbol.kind) &&
            std::filesystem::path(symbol.location.file) == doc.path &&
            position_contains_name(params, name, symbol.location)) {
            return symbol;
        }
    }
    return std::nullopt;
}

bool document_declares_renameable_symbol(const Document& doc, const std::string& name,
                                         const std::vector<Symbol>& symbols) {
    for (const Symbol& symbol : symbols) {
        if (symbol.name == name && renameable_symbol_kind(symbol.kind) &&
            std::filesystem::path(symbol.location.file) == doc.path) {
            return true;
        }
    }
    return false;
}

std::optional<Symbol>
unique_document_declaration_for_references(const Document& doc, const std::string& name,
                                           const ModuleAst* module,
                                           const std::vector<Symbol>& symbols) {
    if (name.empty() || name.find('.') != std::string::npos || module == nullptr) {
        return std::nullopt;
    }
    std::set<std::string> reference_ranges;
    const std::vector<ReferenceLocation> references = references_in(*module, doc, name);
    for (const ReferenceLocation& location : references) {
        reference_ranges.insert(symbol_range_key(location.uri, location.range));
    }
    std::optional<Symbol> declaration;
    for (const Symbol& symbol : symbols) {
        if (symbol.name != name || !renameable_symbol_kind(symbol.kind)) {
            continue;
        }
        if (!reference_ranges.contains(symbol_range_key(symbol, doc))) {
            continue;
        }
        if (declaration.has_value()) {
            return std::nullopt;
        }
        declaration = symbol;
    }
    return declaration;
}

std::optional<std::string> native_identity_for_query(const std::vector<Symbol>& symbols,
                                                     const std::string& query) {
    if (query.empty()) {
        return std::nullopt;
    }
    for (const Symbol& symbol : symbols) {
        if (!symbol.location.file.ends_with(".dd") && symbol.name == query &&
            symbol.native_identity_key.has_value()) {
            return symbol.native_identity_key;
        }
    }
    return std::nullopt;
}

std::optional<std::string> native_identity_for_selection(const AstSelection& selection,
                                                         const ModuleAst* module,
                                                         const std::vector<Symbol>& symbols,
                                                         const std::string& query,
                                                         const SourceLocation& cursor_location) {
    if (module == nullptr || !selection.call_callee || !selection.call_expr.has_value()) {
        return native_identity_for_query(symbols, query);
    }
    try {
        Symbols sema_symbols = collect_symbols(*module);
        FunctionScope scope(sema_symbols);
        scope.local_type_refs = local_type_refs_before_location(*module, cursor_location);
        const Expr& call = *selection.call_expr;
        const std::vector<TypeRef> explicit_args =
            call.kind == ExprKind::TemplateCall ? template_type_refs(call) : std::vector<TypeRef>{};
        if (const std::optional<NativeSignatureMatch> matched = match_native_signature_declaration(
                scope, query, explicit_args, call.children, nullptr);
            matched && matched->declaration != nullptr) {
            if (std::filesystem::path(matched->declaration->location.file).extension() == ".dd") {
                return std::nullopt;
            }
            const std::string identity = native_symbol_identity_key(matched->declaration->identity);
            if (!identity.empty()) {
                return identity;
            }
        }
        if (has_expr_callee(call) && expr_callee(call).front().kind == ExprKind::Member &&
            expr_callee(call).front().children.size() == 1) {
            const Expr& member = expr_callee(call).front();
            const std::optional<TypeRef> static_receiver =
                static_class_receiver_type_ref(scope, member.children.front());
            const TypeRef receiver = static_receiver.value_or(
                infer_expr_type_ast(scope, member.children.front(), nullptr));
            const std::vector<DuduMethodInstantiation> methods =
                static_receiver ? dudu_static_method_instantiations_for_type(
                                      scope.symbols, receiver, member.name, explicit_args)
                                : dudu_method_instantiations_for_type(scope.symbols, receiver,
                                                                      member.name, explicit_args);
            if (!methods.empty()) {
                std::vector<FunctionSignature> signatures;
                signatures.reserve(methods.size());
                for (const DuduMethodInstantiation& method : methods) {
                    signatures.push_back(method.signature);
                }
                if (const std::optional<size_t> matched =
                        matching_signature_index_ast(scope, signatures, call.children);
                    matched && methods[*matched].method != nullptr &&
                    std::filesystem::path(methods[*matched].method->location.file).extension() !=
                        ".dd") {
                    const std::string identity =
                        native_symbol_identity_key(methods[*matched].method->native_identity);
                    return identity.empty() ? std::nullopt : std::optional<std::string>{identity};
                }
            }
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::string dotted_tail(const std::string& query) {
    const size_t dot = query.rfind('.');
    return dot == std::string::npos ? query : query.substr(dot + 1);
}

ProjectIndexSnapshot document_project_index(const Document& doc, bool include_native) {
    try {
        return project_index_for_document(doc, include_native);
    } catch (const std::exception&) {
        return {};
    }
}

const ModuleAst* visible_document_unit(const ProjectIndex* index, const Document& doc) {
    return index == nullptr ? nullptr : &index->visible_unit_for_path(doc.path);
}

ProjectIndexSnapshot workspace_candidate_index(const ProjectIndexSnapshot& workspace_index,
                                               const Document& candidate, bool include_native) {
    if (workspace_index && workspace_index->unit_for_path(candidate.path) != nullptr) {
        return workspace_index;
    }
    return document_project_index(candidate, include_native);
}

} // namespace dudu
