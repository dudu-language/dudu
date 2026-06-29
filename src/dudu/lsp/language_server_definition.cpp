#include "dudu/lsp/language_server_definition.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/project/module_names.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

bool range_contains_position(const SourceRange& range, int line, int character) {
    const int start_line = range.start.line - 1;
    const int start_character = range.start.column - 1;
    const int end_line = range.end.line - 1;
    const int end_character = range.end.column - 1;
    if (line < start_line || line > end_line) {
        return false;
    }
    if (line == start_line && character < start_character) {
        return false;
    }
    if (line == end_line && character > end_character) {
        return false;
    }
    return true;
}

std::string unquote_header(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::vector<std::filesystem::path> pkg_config_include_dirs(const ProjectConfig& config) {
    if (config.pkg_config_packages.empty()) {
        return {};
    }
    const char* pkg_config = std::getenv("PKG_CONFIG");
    const std::string executable = pkg_config == nullptr ? "pkg-config" : std::string(pkg_config);
    std::string command = shell_quote_arg(executable) + " --cflags";
    for (const std::string& package : config.pkg_config_packages) {
        command += " " + shell_quote_arg(package);
    }
    command += " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }
    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    if (pclose(pipe) != 0) {
        return {};
    }
    std::vector<std::filesystem::path> out;
    std::istringstream flags(output);
    std::string flag;
    while (flags >> flag) {
        if (starts_with(flag, "-I") && flag.size() > 2) {
            out.emplace_back(flag.substr(2));
        }
    }
    return out;
}

std::optional<std::filesystem::path> resolve_header_path(const std::filesystem::path& source_dir,
                                                         const ProjectConfig& config,
                                                         const std::string& header) {
    const std::filesystem::path header_path = header;
    std::vector<std::filesystem::path> roots;
    if (header_path.is_absolute()) {
        roots.push_back({});
    } else {
        roots.push_back(source_dir);
        for (const std::string& include_dir : config.include_dirs) {
            roots.push_back(project_path(config, include_dir));
        }
        for (const std::filesystem::path& include_dir : pkg_config_include_dirs(config)) {
            roots.push_back(include_dir);
        }
    }
    for (std::filesystem::path root : roots) {
        std::filesystem::path candidate = root.empty() ? header_path : root / header_path;
        std::error_code error;
        if (std::filesystem::exists(candidate, error) && !error) {
            const std::filesystem::path resolved =
                std::filesystem::weakly_canonical(candidate, error);
            return error ? candidate.lexically_normal() : resolved;
        }
    }
    return std::nullopt;
}

std::optional<std::string> header_definition_json(const Document& doc, const ModuleAst& current,
                                                  const Json* params) {
    const LspPosition position = lsp_position(params);
    for (const ImportDecl& import : current.imports) {
        if (import.kind != ImportKind::ForeignC && import.kind != ImportKind::ForeignCxx &&
            import.kind != ImportKind::ForeignCpp) {
            continue;
        }
        if (!range_contains_position(import.module_range, position.line, position.character)) {
            continue;
        }
        const ProjectConfig config = config_for_file(doc.path);
        const std::optional<std::filesystem::path> resolved =
            resolve_header_path(doc.path.parent_path(), config, unquote_header(import.module_path));
        if (!resolved) {
            return std::nullopt;
        }
        return location_json(file_uri(*resolved), range_json(0, 0, 0));
    }
    return std::nullopt;
}

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

int function_end_line(const FunctionDecl& function) {
    int line = function.location.line;
    for (const Stmt& stmt : function.statements) {
        line = std::max(line, stmt.range.end.line);
        line = std::max(line, stmt.location.line);
    }
    return line;
}

bool function_contains_line(const FunctionDecl& function, int one_based_line) {
    return function.location.line <= one_based_line &&
           one_based_line <= function_end_line(function);
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
        if (!function_contains_line(function, line)) {
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

std::string definition_json(const Document& doc, const Json* params) {
    const ProjectIndex& index = project_index_for_document(doc, false);
    const ModuleAst& current = index.visible_unit_for_path(doc.path);
    if (const std::optional<std::string> header = header_definition_json(doc, current, params)) {
        return *header;
    }
    const AstSelection selection = ast_selection_at(current, params);
    const std::string word = selection.symbol_path.value_or("");
    if (word.empty()) {
        return "null";
    }
    if (const std::optional<std::string> local =
            local_definition_json(doc, current, params, word)) {
        return *local;
    }
    if (const std::optional<std::string> constructor =
            constructor_definition_json(doc, current, selection)) {
        return *constructor;
    }
    const std::vector<Symbol> symbols = symbols_for_module(current, false);
    if (const std::optional<Symbol> exact = exact_symbol_match(symbols, word)) {
        return symbol_definition_json(*exact, doc);
    }
    const ProjectIndex* native_index = nullptr;
    const auto load_native_index = [&]() -> const ProjectIndex* {
        if (native_index == nullptr) {
            native_index = &project_index_for_document(doc, true);
        }
        return native_index;
    };
    const std::optional<ExprPath>& path = selection.expr_path;
    if (path && path->segments.size() >= 2) {
        if (const std::optional<Symbol> class_member =
                class_member_symbol_for_path(current, *path)) {
            return symbol_definition_json(*class_member, doc);
        }
        try {
            const ProjectIndex* native = load_native_index();
            if (const std::optional<Symbol> class_member =
                    class_member_symbol_for_path(native->visible_unit_for_path(doc.path), *path)) {
                return symbol_definition_json(*class_member, doc);
            }
            if (const std::optional<std::string> member_definition =
                    member_definition_json(doc, *path, params, current, native->merged_module())) {
                return *member_definition;
            }
        } catch (const std::exception&) {
        }
    }
    if (const std::optional<Symbol> suffix = unambiguous_suffix_symbol_match(symbols, word)) {
        return symbol_definition_json(*suffix, doc);
    }
    if (const std::optional<std::string> import_definition =
            import_definition_json(doc, index, current, word)) {
        return *import_definition;
    }
    if (path && path->segments.size() >= 2) {
        const std::string path_text = render_expr_path(*path);
        if (path_text != word) {
            return import_definition_json(doc, index, current, path_text).value_or("null");
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
    if (const std::optional<std::string> native_type_target =
            native_type_target_definition_json(doc, word, native->merged_module())) {
        return *native_type_target;
    }
    const std::vector<Symbol> native_symbols =
        symbols_for_module(native->visible_unit_for_path(doc.path), true);
    if (const std::optional<Symbol> exact = exact_symbol_match(native_symbols, word)) {
        return symbol_definition_json(*exact, doc);
    }
    if (const std::optional<Symbol> suffix =
            unambiguous_suffix_symbol_match(native_symbols, word)) {
        return symbol_definition_json(*suffix, doc);
    }
    return "null";
}

} // namespace dudu
