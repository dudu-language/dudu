#include "dudu/language_server_hover.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_local_context.hpp"
#include "dudu/language_server_native_lookup.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/module_names.hpp"
#include "dudu/native_headers.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string doc_comment_before(const Document& doc, int one_based_line) {
    const std::vector<std::string> lines = document_lines(doc.text);
    int row = one_based_line - 2;
    std::vector<std::string> comments;
    while (row >= 0 && row < static_cast<int>(lines.size())) {
        const std::string trimmed = trim_copy(lines[static_cast<size_t>(row)]);
        if (!starts_with(trimmed, "#")) {
            break;
        }
        comments.push_back(trim_copy(trimmed.substr(1)));
        --row;
    }
    std::reverse(comments.begin(), comments.end());
    std::ostringstream out;
    for (size_t i = 0; i < comments.size(); ++i) {
        if (i > 0) {
            out << "\n";
        }
        out << comments[i];
    }
    return out.str();
}

std::optional<std::string> imported_symbol_hover_json(const Document& doc,
                                                      const ProjectIndex& index,
                                                      const ModuleAst& current,
                                                      const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    for (const ImportDecl& import : current.imports) {
        std::string target;
        if (import.kind == ImportKind::Module) {
            const std::string bound = bound_import_name(import);
            const std::vector<std::string> prefixes =
                import.alias.empty() ? std::vector<std::string>{import.module_path, bound}
                                     : std::vector<std::string>{bound};
            for (const std::string& prefix : prefixes) {
                if (word.rfind(prefix + ".", 0) == 0) {
                    target = word.substr(prefix.size() + 1);
                    break;
                }
            }
            if (target.empty()) {
                continue;
            }
        } else if (import.kind == ImportKind::From && bound_import_name(import) == word) {
            target = import.imported_name;
        } else {
            continue;
        }
        const std::filesystem::path file =
            module_path_to_file(doc.path.parent_path(), import.module_path);
        std::error_code error;
        if (!std::filesystem::exists(file, error) || error) {
            continue;
        }
        const ModuleAst* imported = index.imported_unit(current, import);
        if (imported == nullptr) {
            continue;
        }
        for (const Symbol& symbol : symbols_for_module(*imported, false)) {
            if (symbol.name != target) {
                continue;
            }
            return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" +
                   json_escape(symbol.detail) + "`\"}}";
        }
    }
    return std::nullopt;
}

std::optional<std::string> native_alias_hover_json(const std::string& word,
                                                   const ModuleAst& module) {
    if (word.empty()) {
        return std::nullopt;
    }
    const auto build_hover = [&](const NativeTypeDecl& type) -> std::optional<std::string> {
        const std::optional<NativeClassDefinition> target =
            native_alias_target_class_definition(module, type);
        if (!target) {
            return std::nullopt;
        }
        const std::string markdown = "`native type = " + native_type_alias_type_text(type) +
                                     "`\n\nresolves to `" + "native class " + target->name + "`";
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
               "\"},\"range\":" + range_json(type.location) + "}";
    };
    for (const NativeTypeDecl& type : module.native_types) {
        if (type.name == word) {
            return build_hover(type);
        }
    }
    for (const NativeTypeDecl& type : module.native_types) {
        if (symbol_matches(type.name, word)) {
            return build_hover(type);
        }
    }
    return std::nullopt;
}

std::string symbol_hover_json(const Document& doc, const Symbol& symbol) {
    std::string markdown = "`" + symbol.detail + "`";
    if (uri_for_location(symbol.location, doc) == doc.uri) {
        if (const std::string docs = doc_comment_before(doc, symbol.location.line); !docs.empty()) {
            markdown += "\n\n" + docs;
        }
    }
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" + range_json(symbol.location) + "}";
}

std::optional<std::string> member_hover_json(const Document& doc, const ExprPath& path,
                                             const Json* params, const ModuleAst& module) {
    if (params == nullptr || path.segments.size() < 2 ||
        path.segments.front().kind != ExprPathSegmentKind::Name ||
        path.segments.back().kind != ExprPathSegmentKind::Name) {
        return std::nullopt;
    }
    const std::string& receiver = path.segments.front().text;
    const std::string& member = path.segments.back().text;
    const TypeRef type_ref =
        local_type_ref_before_cursor(visible_module_unit(module, doc.path), receiver, params);
    if (!has_type_ref(type_ref)) {
        return std::nullopt;
    }
    const std::set<std::string> candidate_types = member_candidate_types(module, type_ref);
    const auto find_member =
        [&](const std::vector<ClassDecl>& classes) -> std::optional<std::string> {
        for (const ClassDecl& klass : classes) {
            if (!candidate_types.contains(klass.name)) {
                continue;
            }
            for (const FieldDecl& field : klass.fields) {
                if (field.name == member) {
                    return symbol_hover_json(
                        doc, {.name = field.name,
                              .detail = field.name + ": " + type_ref_text(field.type_ref),
                              .location = field.location,
                              .kind = lsp_symbol_kind::Field,
                              .native_identity_key = std::nullopt});
                }
            }
            for (const ConstDecl& constant : klass.constants) {
                if (constant.name == member) {
                    return symbol_hover_json(
                        doc, {.name = constant.name,
                              .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                              .location = constant.location,
                              .kind = lsp_symbol_kind::Constant,
                              .native_identity_key = std::nullopt});
                }
            }
            for (const ConstDecl& field : klass.static_fields) {
                if (field.name == member) {
                    return symbol_hover_json(
                        doc, {.name = field.name,
                              .detail = field.name + ": " + type_ref_text(field.type_ref),
                              .location = field.location,
                              .kind = lsp_symbol_kind::Field,
                              .native_identity_key = std::nullopt});
                }
            }
            for (const FunctionDecl& method : klass.methods) {
                if (method.name == member) {
                    return symbol_hover_json(doc, {.name = method.name,
                                                   .detail = function_detail(method),
                                                   .location = method.location,
                                                   .kind = is_constructor_method_name(method.name)
                                                               ? lsp_symbol_kind::Constructor
                                                               : lsp_symbol_kind::Method,
                                                   .native_identity_key = std::nullopt});
                }
            }
        }
        return std::nullopt;
    };
    if (const std::optional<std::string> native = find_member(module.native_classes)) {
        return native;
    }
    return find_member(module.classes);
}

} // namespace

std::string hover_json(const Document& doc, const std::string& word, const std::string& local_type,
                       const Json* params, std::optional<ExprPath> selected_path) {
    const ProjectIndex* index = nullptr;
    try {
        index = &project_index_for_document(doc, false);
    } catch (const std::exception&) {
        return "null";
    }
    const ModuleAst& current = index->visible_unit_for_path(doc.path);
    std::string query = word;
    if ((query.empty() || !selected_path.has_value()) && params != nullptr) {
        const AstSelection selection = ast_selection_at(current, params);
        if (query.empty()) {
            query = selection.symbol_path.value_or("");
        }
        if (!selected_path.has_value()) {
            selected_path = selection.expr_path;
        }
    }
    const std::vector<Symbol> symbols = visible_symbols_for_document(current, doc, false);
    if (const std::optional<Symbol> exact = exact_symbol_match(symbols, query)) {
        return symbol_hover_json(doc, *exact);
    }
    if (const std::optional<std::string> imported =
            imported_symbol_hover_json(doc, *index, current, query)) {
        return *imported;
    }
    const ProjectIndex* native_index = nullptr;
    const auto load_native_index = [&]() -> const ProjectIndex* {
        if (native_index == nullptr) {
            native_index = &project_index_for_document(doc, true);
        }
        return native_index;
    };
    try {
        const ProjectIndex* native = load_native_index();
        if (const std::optional<std::string> native_alias =
                native_alias_hover_json(query, native->merged_module())) {
            return *native_alias;
        }
        const std::vector<Symbol> native_symbols = visible_symbols_for_document(
            native->visible_unit_for_path(doc.path), doc, true);
        if (const std::optional<Symbol> exact = exact_symbol_match(native_symbols, query)) {
            return symbol_hover_json(doc, *exact);
        }
        if (const std::optional<Symbol> suffix =
                unambiguous_suffix_symbol_match(native_symbols, query)) {
            return symbol_hover_json(doc, *suffix);
        }
    } catch (const std::exception&) {
    }
    if (selected_path.has_value()) {
        try {
            const ProjectIndex* native = load_native_index();
            if (const std::optional<std::string> member_hover =
                    member_hover_json(doc, *selected_path, params, native->merged_module())) {
                return *member_hover;
            }
        } catch (const std::exception&) {
        }
    }
    if (const std::optional<Symbol> suffix = unambiguous_suffix_symbol_match(symbols, query)) {
        return symbol_hover_json(doc, *suffix);
    }
    std::string fallback_local_type = local_type;
    if (fallback_local_type.empty() && !query.empty() && params != nullptr) {
        fallback_local_type =
            substitute_type_ref_text(local_type_ref_before_cursor(current, query, params), {});
    }
    if (!fallback_local_type.empty()) {
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" + json_escape(query) + ": " +
               json_escape(fallback_local_type) + "`\"}}";
    }
    return "null";
}

} // namespace dudu
