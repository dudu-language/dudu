#include "dudu/lsp/language_server_definition.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_declaration_references.hpp"
#include "dudu/lsp/language_server_decorators.hpp"
#include "dudu/lsp/language_server_generic_params.hpp"
#include "dudu/lsp/language_server_header_definition.hpp"
#include "dudu/lsp/language_server_import_references.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_operator.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/project/module_names.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::optional<std::string> member_definition_json(const Document& doc, const ExprPath& path,
                                                  const Json* params, const ModuleAst& current,
                                                  const ModuleAst& module) {
    if (path.segments.size() < 2 || path.segments.front().kind != ExprPathSegmentKind::Name ||
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
                    return location_json(uri_for_location(field.location, doc),
                                         range_json(field.location));
                }
            }
            for (const FunctionDecl& method : klass.methods) {
                if (method.name == member) {
                    return location_json(uri_for_location(method.location, doc),
                                         range_json(method.location));
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

std::optional<std::string> import_definition_json(const Document& doc, const ProjectIndex& index,
                                                  const ModuleAst& current,
                                                  const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    for (const ImportDecl& import : current.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        std::string from_suffix;
        if (import.kind == ImportKind::From) {
            if (word == import.module_path) {
                const std::filesystem::path file =
                    module_path_to_file(doc.path.parent_path(), import.module_path);
                std::error_code error;
                if (std::filesystem::exists(file, error) && !error) {
                    return location_json(file_uri(file), range_json(0, 0, 0));
                }
                continue;
            }
            const std::string bound = bound_import_name(import);
            if (bound != word) {
                if (word.rfind(bound + ".", 0) != 0) {
                    continue;
                }
                from_suffix = word.substr(bound.size());
            }
        }
        std::string target;
        if (import.kind == ImportKind::Module) {
            const std::string bound = bound_import_name(import);
            const std::vector<std::string> prefixes =
                import.alias.empty() ? std::vector<std::string>{import.module_path, bound}
                                     : std::vector<std::string>{bound};
            bool matched = false;
            for (const std::string& prefix : prefixes) {
                if (word == prefix) {
                    matched = true;
                    break;
                }
                if (word.rfind(prefix + ".", 0) == 0) {
                    target = word.substr(prefix.size() + 1);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }
        }
        const std::filesystem::path file =
            module_path_to_file(doc.path.parent_path(), import.module_path);
        std::error_code error;
        if (!std::filesystem::exists(file, error) || error) {
            continue;
        }
        if (import.kind == ImportKind::Module && target.empty()) {
            return location_json(file_uri(file), range_json(0, 0, 0));
        }
        const ModuleAst* imported = index.imported_unit(current, import);
        if (imported == nullptr) {
            continue;
        }
        if (import.kind == ImportKind::From) {
            target = import.imported_name + from_suffix;
        }
        for (const Symbol& symbol : symbols_for_module(*imported, false)) {
            if (symbol.name == target) {
                return location_json(uri_for_location(symbol.location, doc),
                                     range_json(symbol.location));
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> native_type_target_definition_json(const Document& doc,
                                                              const std::string& word,
                                                              const ModuleAst& module) {
    if (word.empty()) {
        return std::nullopt;
    }
    if (const std::optional<NativeClassDefinition> definition =
            native_alias_target_class_definition(module, word)) {
        return location_json(uri_for_location(definition->location, doc),
                             range_json(definition->location));
    }
    return std::nullopt;
}

std::string symbol_definition_json(const Symbol& symbol, const Document& doc) {
    return location_json(uri_for_location(symbol.location, doc), range_json(symbol.location));
}

std::optional<std::string> constructor_definition_json(const Document& doc, const ModuleAst& module,
                                                       const AstSelection& selection) {
    if (!selection.call_callee) {
        return std::nullopt;
    }
    std::vector<std::string> candidates;
    if (selection.symbol) {
        candidates.push_back(*selection.symbol);
    }
    if (selection.expr_path) {
        const ExprPath& path = *selection.expr_path;
        if (!path.segments.empty()) {
            candidates.push_back(path.segments.back().text);
        }
    }
    if (selection.symbol_path) {
        candidates.push_back(*selection.symbol_path);
    }
    std::ranges::sort(candidates);
    candidates.erase(std::ranges::unique(candidates).begin(), candidates.end());
    const auto find_constructor =
        [&](const std::vector<ClassDecl>& classes) -> std::optional<std::string> {
        for (const ClassDecl& klass : classes) {
            if (std::ranges::find(candidates, klass.name) == candidates.end()) {
                continue;
            }
            for (const FunctionDecl& method : klass.methods) {
                if (is_constructor_method_name(method.name)) {
                    return location_json(uri_for_location(method.location, doc),
                                         range_json(method.location));
                }
            }
        }
        return std::nullopt;
    };
    if (const std::optional<std::string> native = find_constructor(module.native_classes)) {
        return native;
    }
    return find_constructor(module.classes);
}

std::optional<std::string> binary_operator_definition_json(const Document& doc,
                                                           const ModuleAst& module,
                                                           const AstSelection& selection,
                                                           const Json* params) {
    if (!selection.operator_expr) {
        return std::nullopt;
    }
    const std::optional<Symbol> symbol = dudu_operator_symbol_for_expr(
        module, *selection.operator_expr, lsp_position(params).line + 1);
    if (!symbol) {
        return std::nullopt;
    }
    return location_json(uri_for_location(symbol->location, doc), range_json(symbol->location));
}

bool location_before_cursor(SourceLocation location, const LspPosition& position) {
    const int line = position.line + 1;
    const int column = position.character + 1;
    if (location.line < line) {
        return true;
    }
    return location.line == line && location.column <= column;
}

void collect_binding_locations_before_cursor(const std::vector<Stmt>& statements,
                                             const LspPosition& position, const std::string& query,
                                             std::optional<SourceLocation>& result) {
    for (const Stmt& stmt : statements) {
        if (stmt.location.line > position.line + 1) {
            continue;
        }
        visit_stmt_binding_names(stmt, [&](const std::string& name, SourceLocation location) {
            if (!result && name == query && location_before_cursor(location, position)) {
                result = location;
            }
        });
        collect_binding_locations_before_cursor(stmt.children, position, query, result);
    }
}

std::optional<std::string> local_definition_json(const Document& doc, const ModuleAst& current,
                                                 const Json* params, const std::string& word) {
    if (word.empty() || word.find('.') != std::string::npos) {
        return std::nullopt;
    }
    const LspPosition position = lsp_position(params);
    const int line = position.line + 1;
    const auto search_function = [&](const FunctionDecl& function) -> std::optional<std::string> {
        if (!function_contains_source_line(function, line)) {
            return std::nullopt;
        }
        for (const ParamDecl& param : function.params) {
            if (param.name == word && location_before_cursor(param.location, position)) {
                return location_json(uri_for_location(param.location, doc),
                                     range_json(param.location));
            }
        }
        std::optional<SourceLocation> local;
        collect_binding_locations_before_cursor(function.statements, position, word, local);
        if (local) {
            return location_json(uri_for_location(*local, doc), range_json(*local));
        }
        return std::nullopt;
    };
    for (const FunctionDecl& function : current.functions) {
        if (const std::optional<std::string> found = search_function(function)) {
            return found;
        }
    }
    for (const ClassDecl& klass : current.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (const std::optional<std::string> found = search_function(method)) {
                return found;
            }
        }
    }
    return std::nullopt;
}

} // namespace

std::string definition_json(const Document& doc, const Json* params,
                            const std::map<std::string, Document>& workspace) {
    const ProjectIndexSnapshot index = project_index_for_document(doc, false);
    const ModuleAst& current = index->visible_unit_for_path(doc.path);
    if (const std::optional<std::string> header =
            native_header_definition_json(doc, current, params)) {
        return *header;
    }
    const AstSelection selection = ast_selection_at(current, params);
    if (const std::optional<MacroEditorSelection> macro = macro_selection_at(current, params)) {
        if (const std::optional<Symbol> symbol =
                macro_symbol_for_reference(*index, current, *macro)) {
            return symbol_definition_json(*symbol, doc);
        }
    }
    std::string word = selection.symbol_path.value_or("");
    if (const std::optional<DecoratorSelection> decorator =
            decorator_selection_at(current, params)) {
        if (builtin_decorator_symbol(*decorator)) {
            return "null";
        }
        word = decorator->name;
    }
    if (word.empty()) {
        return "null";
    }
    if (const std::optional<GenericParamTarget> generic =
            generic_param_target_at(current, lsp_position(params), word)) {
        return location_json(uri_for_location(generic->declaration, doc),
                             range_json(generic->declaration));
    }
    if (const std::optional<SourceLocation> generated =
            macro_generated_definition_location(current, word)) {
        return location_json(uri_for_location(*generated, doc), range_json(*generated));
    }
    if (const std::optional<std::string> references =
            declaration_references_json(doc, current, params, workspace)) {
        return *references;
    }
    if (const std::optional<std::string> local =
            local_definition_json(doc, current, params, word)) {
        return *local;
    }
    if (const std::optional<std::string> op =
            binary_operator_definition_json(doc, current, selection, params)) {
        return *op;
    }
    if (const std::optional<std::string> constructor =
            constructor_definition_json(doc, current, selection)) {
        return *constructor;
    }
    if (word.find('.') != std::string::npos && module_import_target_key(doc, word).has_value()) {
        if (const std::optional<std::string> import_definition =
                import_definition_json(doc, *index, current, word)) {
            return *import_definition;
        }
    }
    ProjectIndexSnapshot native_index;
    const auto load_native_index = [&]() -> const ProjectIndex* {
        if (!native_index) {
            native_index = project_index_for_document(doc, true);
        }
        return native_index.get();
    };
    const std::optional<ExprPath>& path = selection.expr_path;
    if (path && path->segments.size() == 2 && path->segments.front().text == "super") {
        if (const std::optional<Symbol> member = class_member_symbol_for_super(
                current, lsp_position(params).line + 1, path->segments.back().text)) {
            return symbol_definition_json(*member, doc);
        }
    }
    if (path && path->segments.size() >= 2) {
        try {
            const ProjectIndex* native = load_native_index();
            const ModuleAst& visible = native->visible_unit_for_path(doc.path);
            const std::vector<Symbol> native_symbols = symbols_for_module(visible, true);
            if (const std::optional<Symbol> native_namespace =
                    native_namespace_segment_symbol(native_symbols, selection.symbol, word)) {
                return symbol_definition_json(*native_namespace, doc);
            }
            if (const std::optional<Symbol> exact = exact_symbol_match(native_symbols, word)) {
                if (exact->native_identity_key.has_value()) {
                    return symbol_definition_json(*exact, doc);
                }
            }
            if (const std::optional<Symbol> class_member =
                    class_member_symbol_for_path(visible, *path)) {
                return symbol_definition_json(*class_member, doc);
            }
            if (const std::optional<std::string> member_definition =
                    member_definition_json(doc, *path, params, current, visible)) {
                return *member_definition;
            }
        } catch (const std::exception&) {
        }
    }
    const std::vector<Symbol> symbols = symbols_for_module(current, false);
    if (const std::optional<Symbol> exact = exact_symbol_match(symbols, word)) {
        return symbol_definition_json(*exact, doc);
    }
    if (path && path->segments.size() >= 2) {
        if (const std::optional<Symbol> class_member =
                class_member_symbol_for_path(current, *path)) {
            return symbol_definition_json(*class_member, doc);
        }
        try {
            const ProjectIndex* native = load_native_index();
            const ModuleAst& visible = native->visible_unit_for_path(doc.path);
            if (const std::optional<std::string> member_definition =
                    member_definition_json(doc, *path, params, current, visible)) {
                return *member_definition;
            }
        } catch (const std::exception&) {
        }
    }
    if (const std::optional<Symbol> suffix = unambiguous_suffix_symbol_match(symbols, word)) {
        return symbol_definition_json(*suffix, doc);
    }
    if (const std::optional<std::string> import_definition =
            import_definition_json(doc, *index, current, word)) {
        return *import_definition;
    }
    if (path && path->segments.size() >= 2) {
        const std::string path_text = render_expr_path(*path);
        if (path_text != word) {
            return import_definition_json(doc, *index, current, path_text).value_or("null");
        }
    }
    const ProjectIndex* native = nullptr;
    try {
        native = load_native_index();
    } catch (const std::exception&) {
        return "null";
    }
    if (const std::optional<std::string> constructor =
            constructor_definition_json(doc, native->visible_unit_for_path(doc.path), selection)) {
        return *constructor;
    }
    const ModuleAst& native_visible = native->visible_unit_for_path(doc.path);
    if (const std::optional<std::string> native_type_target =
            native_type_target_definition_json(doc, word, native_visible)) {
        return *native_type_target;
    }
    const std::vector<Symbol> native_symbols = symbols_for_module(native_visible, true);
    if (const std::optional<Symbol> native_namespace =
            native_namespace_segment_symbol(native_symbols, selection.symbol, word)) {
        return symbol_definition_json(*native_namespace, doc);
    }
    if (const std::optional<Symbol> exact = exact_symbol_match(native_symbols, word)) {
        return symbol_definition_json(*exact, doc);
    }
    if (const std::optional<Symbol> suffix =
            unambiguous_suffix_symbol_match(native_symbols, word)) {
        return symbol_definition_json(*suffix, doc);
    }
    return "null";
}

std::string definition_json(const Document& doc, const Json* params) {
    return definition_json(doc, params, {});
}

} // namespace dudu
