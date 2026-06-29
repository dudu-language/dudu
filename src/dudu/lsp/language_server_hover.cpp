#include "dudu/lsp/language_server_hover.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/project/module_names.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string hover_markdown(const Symbol& symbol) {
    std::string markdown = "`" + symbol.detail + "`";
    if (symbol.native_identity_key.has_value()) {
        markdown += "\n\nNative identity: `" + *symbol.native_identity_key + "`";
    }
    if (!symbol.doc_comment.empty()) {
        markdown += "\n\n" + symbol.doc_comment;
    }
    return markdown;
}

std::optional<std::string> imported_module_hover_json(const ProjectIndex& index,
                                                      const ModuleAst& current,
                                                      const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    for (const ImportDecl& import : current.imports) {
        if (import.kind != ImportKind::Module || bound_import_name(import) != word) {
            continue;
        }
        const ModuleAst* imported = index.imported_unit(current, import);
        if (imported == nullptr) {
            continue;
        }
        std::string markdown = "`module " + import.module_path + "`";
        if (!imported->doc_comment.empty()) {
            markdown += "\n\n" + imported->doc_comment;
        }
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
               "\"},\"range\":" + range_json(import.location) + "}";
    }
    return std::nullopt;
}

std::optional<std::string> imported_symbol_hover_json(const ProjectIndex& index,
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
        } else if (import.kind == ImportKind::From) {
            const std::string bound = bound_import_name(import);
            if (bound == word) {
                target = import.imported_name;
            } else if (word.rfind(bound + ".", 0) == 0) {
                target = import.imported_name + word.substr(bound.size());
            } else {
                continue;
            }
        } else {
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
            return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" +
                   json_escape(hover_markdown(symbol)) + "\"}}";
        }
    }
    return std::nullopt;
}

std::optional<std::string> native_alias_hover_json(const std::string& word,
                                                   const ModuleAst& module) {
    if (word.empty()) {
        return std::nullopt;
    }
    const NativeClassDefinitionIndex class_index = native_class_definition_index(module);
    const auto build_hover = [&](const NativeTypeDecl& type) -> std::optional<std::string> {
        const std::optional<NativeClassDefinition> target =
            native_alias_target_class_definition(class_index, type);
        if (!target) {
            return std::nullopt;
        }
        const std::string markdown = "`native type = " + native_type_alias_type_text(type) +
                                     "`\n\nresolves to `" + "native class " + target->name + "`";
        const std::string identity = native_symbol_identity_key(type.identity);
        const std::string identity_text =
            identity.empty() ? std::string{} : "\n\nNative identity: `" + identity + "`";
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
               json_escape(identity_text) + "\"},\"range\":" + range_json(type.location) + "}";
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

std::string symbol_hover_json(const Symbol& symbol) {
    const std::string markdown = hover_markdown(symbol);
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" + range_json(symbol.location) + "}";
}

std::optional<std::string> member_hover_json(const ExprPath& path, const Json* params,
                                             const ModuleAst& current, const ModuleAst& module) {
    if (params == nullptr || path.segments.size() < 2 ||
        path.segments.front().kind != ExprPathSegmentKind::Name ||
        path.segments.back().kind != ExprPathSegmentKind::Name) {
        return std::nullopt;
    }
    const std::string& receiver = path.segments.front().text;
    const std::string& member = path.segments.back().text;
    const TypeRef type_ref = local_type_ref_before_cursor(current, receiver, params);
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
                        {.name = field.name,
                         .detail = field.name + ": " + type_ref_text(field.type_ref),
                         .location = field.location,
                         .kind = lsp_symbol_kind::Field,
                         .native_identity_key = std::nullopt,
                         .doc_comment = field.doc_comment});
                }
            }
            for (const ConstDecl& constant : klass.constants) {
                if (constant.name == member) {
                    return symbol_hover_json(
                        {.name = constant.name,
                         .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                         .location = constant.location,
                         .kind = lsp_symbol_kind::Constant,
                         .native_identity_key = std::nullopt,
                         .doc_comment = constant.doc_comment});
                }
            }
            for (const ConstDecl& field : klass.static_fields) {
                if (field.name == member) {
                    return symbol_hover_json(
                        {.name = field.name,
                         .detail = field.name + ": " + type_ref_text(field.type_ref),
                         .location = field.location,
                         .kind = lsp_symbol_kind::Field,
                         .native_identity_key = std::nullopt,
                         .doc_comment = field.doc_comment});
                }
            }
            for (const FunctionDecl& method : klass.methods) {
                if (method.name == member) {
                    return symbol_hover_json({.name = method.name,
                                              .detail = function_detail(method),
                                              .location = method.location,
                                              .kind = is_constructor_method_name(method.name)
                                                          ? lsp_symbol_kind::Constructor
                                                          : lsp_symbol_kind::Method,
                                              .native_identity_key = std::nullopt,
                                              .doc_comment = method.doc_comment});
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

std::string hover_json(const Document& doc, const std::string& word, const Json* params,
                       std::optional<ExprPath> selected_path) {
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
    const std::vector<Symbol> symbols = symbols_for_module(current, false);
    if (const std::optional<Symbol> exact = exact_symbol_match(symbols, query)) {
        return symbol_hover_json(*exact);
    }
    if (const std::optional<std::string> module_hover =
            imported_module_hover_json(*index, current, query)) {
        return *module_hover;
    }
    if (const std::optional<std::string> imported =
            imported_symbol_hover_json(*index, current, query)) {
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
        const std::vector<Symbol> native_symbols =
            symbols_for_module(native->visible_unit_for_path(doc.path), true);
        if (const std::optional<Symbol> exact = exact_symbol_match(native_symbols, query)) {
            return symbol_hover_json(*exact);
        }
        if (const std::optional<Symbol> suffix =
                unambiguous_suffix_symbol_match(native_symbols, query)) {
            return symbol_hover_json(*suffix);
        }
    } catch (const std::exception&) {
    }
    if (selected_path.has_value()) {
        try {
            const ProjectIndex* native = load_native_index();
            if (const std::optional<std::string> member_hover =
                    member_hover_json(*selected_path, params, current, native->merged_module())) {
                return *member_hover;
            }
        } catch (const std::exception&) {
        }
    }
    if (const std::optional<Symbol> suffix = unambiguous_suffix_symbol_match(symbols, query)) {
        return symbol_hover_json(*suffix);
    }
    std::string local_type =
        !query.empty() && params != nullptr
            ? substitute_type_ref_text(local_type_ref_before_cursor(current, query, params), {})
            : std::string{};
    if (local_type.empty() && !query.empty() && params != nullptr) {
        try {
            const ProjectIndex* native = load_native_index();
            local_type = substitute_type_ref_text(
                local_type_ref_before_cursor(native->visible_unit_for_path(doc.path), query,
                                             params),
                {});
        } catch (const std::exception&) {
        }
    }
    if (!local_type.empty()) {
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" + json_escape(query) + ": " +
               json_escape(local_type) + "`\"}}";
    }
    return "null";
}

} // namespace dudu
