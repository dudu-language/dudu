#include "dudu/lsp/language_server_code_actions.hpp"

#include "dudu/core/text.hpp"
#include "dudu/format/format_imports.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

struct CodeActionEdit {
    std::string title;
    std::string kind;
    TextEdit edit;
};

ProjectIndexSnapshot document_project_index(const Document& doc) {
    try {
        return project_index_for_document(doc, false);
    } catch (const std::exception&) {
        return {};
    }
}

const ModuleAst* visible_document_unit(const ProjectIndex* index, const Document& doc) {
    return index == nullptr ? nullptr : &index->visible_unit_for_path(doc.path);
}

std::string code_action_json(const Document& doc, const CodeActionEdit& action) {
    return "{\"title\":\"" + json_escape(action.title) + "\",\"kind\":\"" +
           json_escape(action.kind) + "\",\"edit\":{\"changes\":{\"" + json_escape(doc.uri) +
           "\":[{\"range\":" + action.edit.range + ",\"newText\":\"" +
           json_escape(action.edit.new_text) + "\"}]}}}";
}

std::optional<TextEdit> organize_imports_edit(const Document& doc, const ModuleAst& module) {
    const std::vector<std::string> lines = document_lines(doc.text);
    const std::optional<OrganizedImportBlock> organized =
        organized_leading_import_block(lines, module);
    if (!organized) {
        return std::nullopt;
    }
    return TextEdit{.range = range_json(static_cast<int>(organized->start_line), 0,
                                        static_cast<int>(organized->end_line), 0),
                    .new_text = organized->replacement_text};
}

bool importable_symbol_kind(int kind) {
    return kind == lsp_symbol_kind::Class || kind == lsp_symbol_kind::Enum ||
           kind == lsp_symbol_kind::Function || kind == lsp_symbol_kind::Constant ||
           kind == lsp_symbol_kind::Struct;
}

size_t import_insertion_line(const Document& doc, const ModuleAst& module) {
    if (!module.imports.empty()) {
        int line = 0;
        for (const ImportDecl& import : module.imports) {
            line = std::max(line, import.range.end.line);
        }
        return static_cast<size_t>(line);
    }
    const std::vector<std::string> lines = document_lines(doc.text);
    size_t current = 0;
    while (current < lines.size() && trim_string(lines[current]).empty()) {
        ++current;
    }
    return current;
}

bool import_already_present(const ModuleAst& module, const std::string& module_path,
                            const std::string& name) {
    for (const ImportDecl& import : module.imports) {
        if (import.kind == ImportKind::From && import.module_path == module_path &&
            bound_import_name(import) == name) {
            return true;
        }
        if (import.kind == ImportKind::Module && bound_import_name(import) == name) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> module_path_for_import(const std::filesystem::path& base,
                                                  const std::filesystem::path& file) {
    std::error_code error;
    std::filesystem::path relative = std::filesystem::relative(file, base, error);
    if (error || relative.empty() || relative.is_absolute()) {
        relative = file.filename();
    }
    relative.replace_extension();
    if (relative.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> parts;
    for (const std::filesystem::path& part : relative) {
        const std::string text = part.string();
        if (text.empty() || text == ".") {
            continue;
        }
        if (!valid_identifier(text)) {
            return std::nullopt;
        }
        parts.push_back(text);
    }
    if (parts.empty()) {
        return std::nullopt;
    }
    std::ostringstream out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out << ".";
        }
        out << parts[i];
    }
    return out.str();
}

std::optional<std::string> range_json_from_lsp_range(const Json* range) {
    const Json* start = range == nullptr ? nullptr : range->get("start");
    const Json* end = range == nullptr ? nullptr : range->get("end");
    const int start_line = optional_int_value(start == nullptr ? nullptr : start->get("line"), -1);
    const int start_character =
        optional_int_value(start == nullptr ? nullptr : start->get("character"), -1);
    const int end_line = optional_int_value(end == nullptr ? nullptr : end->get("line"), -1);
    const int end_character =
        optional_int_value(end == nullptr ? nullptr : end->get("character"), -1);
    if (start_line < 0 || start_character < 0 || end_line < 0 || end_character < 0) {
        return std::nullopt;
    }
    return range_json(start_line, start_character, end_line, end_character);
}

std::optional<CodeActionEdit>
missing_import_action(const Document& doc, const std::string& name, const ModuleAst& module,
                      const std::map<std::string, Document>& workspace) {
    std::optional<Symbol> match_symbol;
    std::optional<std::filesystem::path> match_path;
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        if (same_path(candidate.path, doc.path)) {
            continue;
        }
        std::vector<Symbol> candidate_symbols;
        const ProjectIndexSnapshot candidate_index = document_project_index(candidate);
        const ModuleAst* candidate_unit = visible_document_unit(candidate_index.get(), candidate);
        if (candidate_unit == nullptr) {
            continue;
        }
        candidate_symbols = symbols_for_module(*candidate_unit, false);
        for (const Symbol& symbol : candidate_symbols) {
            if (symbol.name != name || !importable_symbol_kind(symbol.kind)) {
                continue;
            }
            const std::filesystem::path declaration_path = symbol.location.file.empty()
                                                               ? candidate.path
                                                               : std::filesystem::path(
                                                                     symbol.location.file.str());
            if (match_symbol) {
                if (same_path(*match_path, declaration_path) &&
                    match_symbol->location.line == symbol.location.line &&
                    match_symbol->location.column == symbol.location.column &&
                    match_symbol->kind == symbol.kind) {
                    continue;
                }
                return std::nullopt;
            }
            match_symbol = symbol;
            match_path = declaration_path;
        }
    }
    if (!match_symbol || !match_path) {
        return std::nullopt;
    }
    const std::optional<std::string> module_path =
        module_path_for_import(doc.path.parent_path(), *match_path);
    if (!module_path || import_already_present(module, *module_path, name)) {
        return std::nullopt;
    }
    const int line = static_cast<int>(import_insertion_line(doc, module));
    const std::string edit_text = "from " + *module_path + " import " + name + "\n";
    return CodeActionEdit{
        .title = "Import " + name + " from " + *module_path,
        .kind = "quickfix",
        .edit = TextEdit{.range = range_json(line, 0, line, 0), .new_text = edit_text}};
}

std::vector<std::string> missing_import_actions(const Document& doc, const Json* params,
                                                const ModuleAst* module,
                                                const std::map<std::string, Document>& workspace) {
    std::vector<std::string> out;
    if (module == nullptr) {
        return out;
    }
    const Json* context = params == nullptr ? nullptr : params->get("context");
    const Json* diagnostics = context == nullptr ? nullptr : context->get("diagnostics");
    const JsonArray* array = diagnostics == nullptr ? nullptr : diagnostics->array();
    if (array == nullptr) {
        return out;
    }
    std::set<std::string> seen;
    for (const Json& diagnostic : *array) {
        if (string_value(diagnostic.get("code")) != "dudu.sema.unknown_identifier") {
            continue;
        }
        const Json* data = diagnostic.get("data");
        const std::string name = string_value(data == nullptr ? nullptr : data->get("name"));
        if (!valid_identifier(name) || !seen.insert(name).second) {
            continue;
        }
        const std::optional<CodeActionEdit> action =
            missing_import_action(doc, name, *module, workspace);
        if (action) {
            out.push_back(code_action_json(doc, *action));
        }
    }
    return out;
}

std::vector<std::string> lint_actions(const Document& doc, const Json* params) {
    std::vector<std::string> out;
    const Json* context = params == nullptr ? nullptr : params->get("context");
    const Json* diagnostics = context == nullptr ? nullptr : context->get("diagnostics");
    const JsonArray* array = diagnostics == nullptr ? nullptr : diagnostics->array();
    if (array == nullptr) {
        return out;
    }
    std::set<int> seen_lines;
    for (const Json& diagnostic : *array) {
        if (string_value(diagnostic.get("source")) != "dudu/lint") {
            continue;
        }
        const std::string code = string_value(diagnostic.get("code"));
        const bool unreachable = code == "dudu.lint.unreachable";
        const bool unused_local = code == "dudu.lint.unused";
        if (!unreachable && !unused_local) {
            continue;
        }
        const Json* data = diagnostic.get("data");
        const std::optional<std::string> fix_range =
            range_json_from_lsp_range(data == nullptr ? nullptr : data->get("fixRange"));
        const Json* start = fix_range ? (data->get("fixRange")->get("start")) : nullptr;
        const int line = optional_int_value(start == nullptr ? nullptr : start->get("line"), -1);
        if (!fix_range || line < 0 || !seen_lines.insert(line).second) {
            continue;
        }
        const std::string title =
            unused_local ? "Remove unused local" : "Remove unreachable statement";
        out.push_back(code_action_json(
            doc, CodeActionEdit{.title = title,
                                .kind = "quickfix",
                                .edit = TextEdit{.range = *fix_range, .new_text = ""}}));
    }
    return out;
}

} // namespace

std::string code_actions_json(const Document& doc, const Json* params,
                              const std::map<std::string, Document>& workspace) {
    std::vector<std::string> actions;
    const ProjectIndexSnapshot index = document_project_index(doc);
    const ModuleAst* current = visible_document_unit(index.get(), doc);
    actions.push_back("{\"title\":\"Format document\",\"kind\":\"source.format\","
                      "\"command\":{\"title\":\"Format document\","
                      "\"command\":\"editor.action.formatDocument\"}}");
    if (current != nullptr) {
        if (const std::optional<TextEdit> edit = organize_imports_edit(doc, *current)) {
            actions.push_back(code_action_json(doc, CodeActionEdit{.title = "Organize imports",
                                                                   .kind = "source.organizeImports",
                                                                   .edit = *edit}));
        }
    }
    for (const std::string& action : missing_import_actions(doc, params, current, workspace)) {
        actions.push_back(action);
    }
    for (const std::string& action : lint_actions(doc, params)) {
        actions.push_back(action);
    }
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << actions[i];
    }
    out << "]";
    return out.str();
}

} // namespace dudu
