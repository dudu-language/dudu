#include "dudu/language_server_code_actions.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/parser.hpp"

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

int line_count(const std::string& text) {
    return static_cast<int>(std::count(text.begin(), text.end(), '\n')) +
           (text.empty() || text.back() == '\n' ? 0 : 1);
}

std::optional<ModuleAst> parsed_document(const Document& doc) {
    try {
        return parse_source(doc.text, doc.path);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::string> document_lines(const std::string& text) {
    std::istringstream in(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::optional<TextEdit> organize_imports_edit(const Document& doc, const ModuleAst& module) {
    if (module.imports.size() < 2) {
        return std::nullopt;
    }
    const std::vector<std::string> lines = document_lines(doc.text);
    size_t start = 0;
    while (start < lines.size() && trim_copy(lines[start]).empty()) {
        ++start;
    }
    if (start >= lines.size()) {
        return std::nullopt;
    }

    std::vector<ImportDecl> imports = module.imports;
    std::sort(imports.begin(), imports.end(), [](const ImportDecl& lhs, const ImportDecl& rhs) {
        return lhs.location.line < rhs.location.line;
    });
    if (imports.front().location.line != static_cast<int>(start + 1)) {
        return std::nullopt;
    }

    std::vector<std::string> import_lines;
    size_t end = start;
    for (const ImportDecl& import : imports) {
        if (import.source_text.empty() || import.location.line != static_cast<int>(end + 1)) {
            break;
        }
        import_lines.push_back(import.source_text);
        ++end;
    }
    if (import_lines.size() < 2) {
        return std::nullopt;
    }
    std::vector<std::string> sorted = import_lines;
    std::sort(sorted.begin(), sorted.end(), [](const std::string& lhs, const std::string& rhs) {
        return trim_copy(lhs) < trim_copy(rhs);
    });
    if (sorted == import_lines) {
        return std::nullopt;
    }
    std::ostringstream replacement;
    for (const std::string& import : sorted) {
        replacement << import << "\n";
    }
    return TextEdit{.range = range_json(static_cast<int>(start), 0, static_cast<int>(end), 0),
                    .new_text = replacement.str()};
}

std::optional<std::string> remove_line_action(const Document& doc, int line,
                                              const std::string& title) {
    std::vector<std::string> lines;
    std::istringstream in(doc.text);
    std::string text_line;
    while (std::getline(in, text_line)) {
        lines.push_back(text_line);
    }
    if (line < 0 || line >= static_cast<int>(lines.size())) {
        return std::nullopt;
    }
    const bool has_next = line + 1 <= line_count(doc.text);
    const std::string range =
        has_next
            ? range_json(line, 0, line + 1, 0)
            : range_json(line, 0, line, static_cast<int>(lines[static_cast<size_t>(line)].size()));
    return "{\"title\":\"" + json_escape(title) +
           "\",\"kind\":\"quickfix\","
           "\"edit\":{\"changes\":{\"" +
           json_escape(doc.uri) + "\":[{\"range\":" + range + ",\"newText\":\"\"}]}}}";
}

bool importable_symbol_kind(int kind) {
    return kind == 5 || kind == 10 || kind == 12 || kind == 14 || kind == 23;
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
    while (current < lines.size() && trim_copy(lines[current]).empty()) {
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

std::optional<std::string> missing_import_action(const Document& doc, const std::string& name,
                                                 const ModuleAst& module,
                                                 const std::map<std::string, Document>& workspace) {
    std::optional<Document> match_doc;
    std::optional<Symbol> match_symbol;
    for (const auto& [uri, candidate] : workspace) {
        (void)uri;
        if (same_path(candidate.path, doc.path)) {
            continue;
        }
        for (const Symbol& symbol : symbols_for_document(candidate, false)) {
            if (symbol.name != name || !importable_symbol_kind(symbol.kind)) {
                continue;
            }
            if (match_symbol) {
                return std::nullopt;
            }
            match_doc = candidate;
            match_symbol = symbol;
        }
    }
    if (!match_doc || !match_symbol) {
        return std::nullopt;
    }
    const std::optional<std::string> module_path =
        module_path_for_import(doc.path.parent_path(), match_doc->path);
    if (!module_path || import_already_present(module, *module_path, name)) {
        return std::nullopt;
    }
    const int line = static_cast<int>(import_insertion_line(doc, module));
    const std::string edit_text = "from " + *module_path + " import " + name + "\n";
    return "{\"title\":\"Import " + json_escape(name) + " from " + json_escape(*module_path) +
           "\",\"kind\":\"quickfix\",\"edit\":{\"changes\":{\"" + json_escape(doc.uri) +
           "\":[{\"range\":" + range_json(line, 0, line, 0) + ",\"newText\":\"" +
           json_escape(edit_text) + "\"}]}}}";
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
        const std::optional<std::string> action =
            missing_import_action(doc, name, *module, workspace);
        if (action) {
            out.push_back(*action);
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
        const Json* range = diagnostic.get("range");
        const Json* start = range == nullptr ? nullptr : range->get("start");
        const int line = int_value(start == nullptr ? nullptr : start->get("line"), -1);
        if (line < 0 || !seen_lines.insert(line).second) {
            continue;
        }
        const std::string title =
            unused_local ? "Remove unused local" : "Remove unreachable statement";
        if (const std::optional<std::string> action = remove_line_action(doc, line, title)) {
            out.push_back(*action);
        }
    }
    return out;
}

} // namespace

std::string code_actions_json(const Document& doc, const Json* params,
                              const std::map<std::string, Document>& workspace) {
    std::vector<std::string> actions;
    const std::optional<ModuleAst> module = parsed_document(doc);
    actions.push_back("{\"title\":\"Format document\",\"kind\":\"source.format\","
                      "\"command\":{\"title\":\"Format document\","
                      "\"command\":\"editor.action.formatDocument\"}}");
    if (module) {
        if (const std::optional<TextEdit> edit = organize_imports_edit(doc, *module)) {
            actions.push_back("{\"title\":\"Organize imports\",\"kind\":\"source.organizeImports\","
                              "\"edit\":{\"changes\":{\"" +
                              json_escape(doc.uri) + "\":[{\"range\":" + edit->range +
                              ",\"newText\":\"" + json_escape(edit->new_text) + "\"}]}}}");
        }
    }
    for (const std::string& action :
         missing_import_actions(doc, params, module ? &*module : nullptr, workspace)) {
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
