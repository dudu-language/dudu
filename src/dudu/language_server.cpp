#include "dudu/language_server.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/format.hpp"
#include "dudu/language_server_code_actions.hpp"
#include "dudu/language_server_diagnostics.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_references.hpp"
#include "dudu/language_server_semantic_tokens.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/language_server_symbol_results.hpp"
#include "dudu/language_server_types.hpp"
#include "dudu/language_server_workspace.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/sema.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

class LanguageServer {
  public:
    LanguageServer(std::istream& in, std::ostream& out, std::ostream& err)
        : in_(in), out_(out), err_(err) {
    }

    int run() {
        while (std::optional<std::string> body = read_message()) {
            try {
                handle_message(*body);
            } catch (const std::exception& error) {
                err_ << "dudu lsp: " << error.what() << '\n';
            }
            if (exit_) {
                return 0;
            }
        }
        return 0;
    }

  private:
    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    std::map<std::string, Document> documents_;
    bool shutdown_ = false;
    bool exit_ = false;

    std::optional<std::string> read_message() {
        std::string line;
        size_t content_length = 0;
        while (std::getline(in_, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                break;
            }
            constexpr std::string_view header = "Content-Length:";
            if (line.rfind(header, 0) == 0) {
                content_length = static_cast<size_t>(std::stoul(line.substr(header.size())));
            }
        }
        if (content_length == 0) {
            return std::nullopt;
        }
        std::string body(content_length, '\0');
        in_.read(body.data(), static_cast<std::streamsize>(body.size()));
        if (in_.gcount() != static_cast<std::streamsize>(body.size())) {
            return std::nullopt;
        }
        return body;
    }

    void write_message(const std::string& body) {
        out_ << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        out_.flush();
    }

    void respond(const Json& id, const std::string& result) {
        std::ostringstream body;
        body << "{\"jsonrpc\":\"2.0\",\"id\":" << id_json(id) << ",\"result\":" << result << "}";
        write_message(body.str());
    }

    void notify(std::string_view method, const std::string& params) {
        std::ostringstream body;
        body << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method << "\",\"params\":" << params << "}";
        write_message(body.str());
    }

    static std::string id_json(const Json& id) {
        if (id.is_null()) {
            return "null";
        }
        if (const std::string* text = id.string()) {
            return "\"" + json_escape(*text) + "\"";
        }
        if (const double* number = std::get_if<double>(&id.value)) {
            std::ostringstream out;
            out << static_cast<long long>(*number);
            return out.str();
        }
        return "null";
    }

    void handle_message(const std::string& body) {
        const Json message = JsonParser(body).parse();
        const Json* method_json = message.get("method");
        if (method_json == nullptr) {
            return;
        }
        const std::string method = string_value(method_json);
        const Json* id = message.get("id");
        const Json* params = message.get("params");

        if (method == "initialize") {
            if (id != nullptr) {
                respond(*id, initialize_result());
            }
        } else if (method == "shutdown") {
            shutdown_ = true;
            if (id != nullptr) {
                respond(*id, "null");
            }
        } else if (method == "exit") {
            exit_ = true;
        } else if (method == "textDocument/didOpen") {
            did_open(params);
        } else if (method == "textDocument/didChange") {
            did_change(params);
        } else if (method == "textDocument/didSave") {
            did_save(params);
        } else if (method == "textDocument/formatting") {
            if (id != nullptr) {
                respond(*id, formatting_result(params));
            }
        } else if (method == "textDocument/documentSymbol") {
            if (id != nullptr) {
                respond(*id, document_symbol_result(params));
            }
        } else if (method == "textDocument/semanticTokens/full") {
            if (id != nullptr) {
                respond(*id, semantic_tokens_result(params));
            }
        } else if (method == "textDocument/definition") {
            if (id != nullptr) {
                respond(*id, definition_result(params));
            }
        } else if (method == "textDocument/references") {
            if (id != nullptr) {
                respond(*id, references_result(params));
            }
        } else if (method == "textDocument/rename") {
            if (id != nullptr) {
                respond(*id, rename_result(params));
            }
        } else if (method == "textDocument/codeAction") {
            if (id != nullptr) {
                respond(*id, code_action_result(params));
            }
        } else if (method == "textDocument/hover") {
            if (id != nullptr) {
                respond(*id, hover_result(params));
            }
        } else if (method == "textDocument/completion") {
            if (id != nullptr) {
                respond(*id, completion_result(params));
            }
        } else if (method == "completionItem/resolve") {
            if (id != nullptr) {
                respond(*id, completion_resolve_result(params));
            }
        } else if (method == "textDocument/signatureHelp") {
            if (id != nullptr) {
                respond(*id, signature_help_result(params));
            }
        } else if (method == "workspace/symbol") {
            if (id != nullptr) {
                respond(*id, workspace_symbol_result(params));
            }
        } else if (id != nullptr) {
            respond(*id, "null");
        }
        if (shutdown_ && method == "exit") {
            exit_ = true;
        }
    }

    static std::string initialize_result() {
        return "{\"capabilities\":{"
               "\"textDocumentSync\":2,"
               "\"documentFormattingProvider\":true,"
               "\"documentSymbolProvider\":true,"
               "\"definitionProvider\":true,"
               "\"referencesProvider\":true,"
               "\"renameProvider\":true,"
               "\"codeActionProvider\":true,"
               "\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[\"namespace\",\"type\","
               "\"class\",\"enum\",\"function\",\"method\",\"variable\",\"parameter\","
               "\"property\",\"enumMember\",\"macro\",\"keyword\",\"number\",\"string\","
               "\"operator\"],\"tokenModifiers\":[\"declaration\",\"definition\",\"readonly\","
               "\"static\",\"native\",\"unresolved\"]},\"full\":true},"
               "\"hoverProvider\":true,"
               "\"completionProvider\":{\"resolveProvider\":true,\"triggerCharacters\":[\".\"]},"
               "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},"
               "\"workspaceSymbolProvider\":true"
               "},\"serverInfo\":{\"name\":\"duc lsp\",\"version\":\"0.1.0\"}}";
    }

    void did_open(const Json* params) {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        if (text_document == nullptr) {
            return;
        }
        const std::string uri = string_value(text_document->get("uri"));
        documents_[uri] = {.uri = uri,
                           .path = file_uri_to_path(uri),
                           .text = string_value(text_document->get("text"))};
        publish_diagnostics(uri);
    }

    void did_change(const Json* params) {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        const Json* changes = params == nullptr ? nullptr : params->get("contentChanges");
        const JsonArray* array = changes == nullptr ? nullptr : changes->array();
        if (text_document == nullptr || array == nullptr || array->empty()) {
            return;
        }
        const std::string uri = string_value(text_document->get("uri"));
        Document& doc = documents_[uri];
        doc.uri = uri;
        doc.path = file_uri_to_path(uri);
        doc.text = string_value(array->back().get("text"));
        publish_diagnostics(uri);
    }

    void did_save(const Json* params) {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        if (text_document == nullptr) {
            return;
        }
        publish_diagnostics(string_value(text_document->get("uri")));
    }

    void publish_diagnostics(const std::string& uri) {
        const auto found = documents_.find(uri);
        if (found == documents_.end()) {
            return;
        }
        std::ostringstream params;
        params << "{\"uri\":\"" << json_escape(uri) << "\",\"diagnostics\":[";
        const std::vector<Diagnostic> diagnostics = diagnostics_for_document(found->second);
        for (size_t i = 0; i < diagnostics.size(); ++i) {
            if (i > 0) {
                params << ',';
            }
            params << diagnostic_json(diagnostics[i]);
        }
        params << "]}";
        notify("textDocument/publishDiagnostics", params.str());
    }

    std::string semantic_tokens_result(const Json* params) const {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        const std::string uri =
            text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
        const auto found = documents_.find(uri);
        if (found == documents_.end()) {
            return "{\"data\":[]}";
        }
        ModuleAst module;
        try {
            module = parse_source(found->second.text, found->second.path);
        } catch (const std::exception&) {
            return "{\"data\":[]}";
        }
        return semantic_tokens_json(module);
    }

    std::string formatting_result(const Json* params) const {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        const std::string uri =
            text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
        const auto found = documents_.find(uri);
        if (found == documents_.end()) {
            return "[]";
        }
        const std::string formatted = format_source(found->second.text);
        std::ostringstream out;
        out << "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":"
            << line_count(found->second.text) << ",\"character\":0}},\"newText\":\""
            << json_escape(formatted) << "\"}]";
        return out.str();
    }

    static int line_count(const std::string& text) {
        int lines = 0;
        for (const char c : text) {
            if (c == '\n') {
                ++lines;
            }
        }
        return lines + 1;
    }

    std::string document_symbol_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "[]";
        }
        return document_symbols_json(*doc);
    }

    std::string workspace_symbol_result(const Json* params) const {
        const std::string query =
            params == nullptr ? std::string{} : string_value(params->get("query"));
        return workspace_symbols_json(query, workspace_documents(documents_), documents_);
    }

    std::string definition_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        if (const std::optional<std::string> header = header_definition_result(*doc, params)) {
            return *header;
        }
        const std::string word = symbol_at(*doc, params);
        for (const Symbol& symbol : symbols_for_document(*doc)) {
            if (symbol_matches(symbol.name, word)) {
                std::ostringstream out;
                out << "{\"uri\":\"" << json_escape(uri_for_location(symbol.location, *doc))
                    << "\",\"range\":" << range_json(symbol.location) << "}";
                return out.str();
            }
        }
        if (const std::optional<std::string> member_definition =
                member_definition_result(*doc, word)) {
            return *member_definition;
        }
        if (const std::optional<std::string> import_definition =
                import_definition_result(*doc, word)) {
            return *import_definition;
        }
        return "null";
    }

    std::optional<std::string> header_definition_result(const Document& doc,
                                                        const Json* params) const {
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
            const std::optional<std::string> header = header_import_at(line, target_character);
            if (!header) {
                return std::nullopt;
            }
            const ProjectConfig config = config_for_file(doc.path);
            const std::optional<std::filesystem::path> resolved =
                resolve_header_path(doc.path.parent_path(), config, *header);
            if (!resolved) {
                return std::nullopt;
            }
            return location_json(file_uri(*resolved), range_json(0, 0, 0));
        }
        return std::nullopt;
    }

    static std::optional<std::string> header_import_at(const std::string& line, int character) {
        static const std::regex import_regex(R"DD(^\s*import\s+(?:c|cpp)\s+"([^"]+)")DD");
        std::smatch match;
        if (!std::regex_search(line, match, import_regex)) {
            return std::nullopt;
        }
        const int start = static_cast<int>(match.position(1));
        const int end = start + static_cast<int>(match.length(1));
        return character >= start && character <= end ? std::optional<std::string>{match[1].str()}
                                                      : std::nullopt;
    }

    static std::optional<std::filesystem::path>
    resolve_header_path(const std::filesystem::path& source_dir, const ProjectConfig& config,
                        const std::string& header) {
        const std::filesystem::path header_path = header;
        std::vector<std::filesystem::path> roots;
        if (header_path.is_absolute()) {
            roots.push_back({});
        } else {
            roots.push_back(source_dir);
            for (const std::string& include_dir : config.include_dirs) {
                const std::filesystem::path path = include_dir;
                roots.push_back(path.is_absolute() ? path : source_dir / path);
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

    static std::vector<std::filesystem::path> pkg_config_include_dirs(const ProjectConfig& config) {
        if (config.pkg_config_packages.empty()) {
            return {};
        }
        const char* pkg_config = std::getenv("PKG_CONFIG");
        const std::string executable =
            pkg_config == nullptr ? "pkg-config" : std::string(pkg_config);
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

    std::optional<std::string> member_definition_result(const Document& doc,
                                                        const std::string& word) const {
        const size_t dot = word.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= word.size()) {
            return std::nullopt;
        }
        const std::string receiver = word.substr(0, dot);
        const std::string member = word.substr(dot + 1);
        const std::string type = local_type_before_cursor(doc, receiver);
        if (type.empty()) {
            return std::nullopt;
        }
        try {
            ModuleAst module = parse_source(doc.text, doc.path);
            const ProjectConfig config = config_for_file(doc.path);
            merge_native_header_types(module,
                                      {.config = config, .source_dir = doc.path.parent_path()});
            const std::set<std::string> candidate_types = member_candidate_types(module, type);
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
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }

    std::optional<std::string> import_definition_result(const Document& doc,
                                                        const std::string& word) const {
        if (word.empty()) {
            return std::nullopt;
        }
        const std::string root = word.substr(0, word.find('.'));
        try {
            const ModuleAst module = parse_source(doc.text, doc.path);
            for (const ImportDecl& import : module.imports) {
                if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
                    continue;
                }
                if (import.kind == ImportKind::Module && bound_import_name(import) != root &&
                    import.module_path != root) {
                    continue;
                }
                if (import.kind == ImportKind::From && bound_import_name(import) != word) {
                    continue;
                }
                const std::filesystem::path file =
                    module_path_to_file(doc.path.parent_path(), import.module_path);
                std::error_code error;
                if (!std::filesystem::exists(file, error) || error) {
                    continue;
                }
                if (import.kind == ImportKind::Module) {
                    return location_json(file_uri(file), range_json(0, 0, 0));
                }
                std::ifstream input(file);
                if (!input) {
                    continue;
                }
                const std::string text{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
                const Document imported{
                    .uri = file_uri(file),
                    .path = file,
                    .text = text,
                };
                for (const Symbol& symbol : symbols_for_document(imported, false)) {
                    if (symbol.name == import.imported_name) {
                        return location_json(uri_for_location(symbol.location, imported),
                                             range_json(symbol.location));
                    }
                }
            }
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }

    std::string references_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "[]";
        }
        return references_json(*doc, params, workspace_documents(documents_));
    }

    std::string rename_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        return rename_json(*doc, params, workspace_documents(documents_));
    }

    std::string code_action_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "[]";
        }
        return code_actions_json(*doc, params, workspace_documents(documents_));
    }

    std::string hover_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        const std::string word = symbol_at(*doc, params);
        for (const Symbol& symbol : symbols_for_document(*doc)) {
            if (symbol_matches(symbol.name, word)) {
                std::string markdown = "`" + symbol.detail + "`";
                if (uri_for_location(symbol.location, *doc) == doc->uri) {
                    if (const std::string docs = doc_comment_before(*doc, symbol.location.line);
                        !docs.empty()) {
                        markdown += "\n\n" + docs;
                    }
                }
                return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
                       "\"},\"range\":" + range_json(symbol.location) + "}";
            }
        }
        if (const std::string type = local_type_before_cursor(*doc, word, params); !type.empty()) {
            return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" + json_escape(word) + ": " +
                   json_escape(type) + "`\"}}";
        }
        return "null";
    }

    static std::string doc_comment_before(const Document& doc, int one_based_line) {
        std::vector<std::string> lines;
        std::istringstream in(doc.text);
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
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

    std::string completion_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc != nullptr) {
            if (const std::optional<std::string> member_target =
                    member_completion_target(*doc, params)) {
                return member_completion_result(*doc, *member_target);
            }
        }
        std::ostringstream out;
        out << "[";
        bool first = true;
        const auto add = [&](std::string_view label, int kind, std::string_view detail) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind
                << ",\"detail\":\"" << json_escape(detail) << "\"}";
        };
        const auto add_snippet = [&](std::string_view label, std::string_view detail,
                                     std::string_view insert_text) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":15,\"detail\":\""
                << json_escape(detail) << "\",\"insertText\":\"" << json_escape(insert_text)
                << "\",\"insertTextFormat\":2}";
        };
        for (std::string_view keyword :
             {"class", "def", "enum", "import", "return", "for", "if", "elif", "else", "while",
              "try", "except", "True", "False", "None"}) {
            add(keyword, 14, "keyword");
        }
        add_snippet("def", "snippet", "def ${1:name}(${2:args}) -> ${3:i32}:\n    ${0:return 0}");
        add_snippet("class", "snippet", "class ${1:Name}:\n    ${0:field: i32}");
        add_snippet("if", "snippet", "if ${1:condition}:\n    ${0:pass}");
        add_snippet("for", "snippet", "for ${1:item}: ${2:i32} in ${3:items}:\n    ${0:pass}");
        add_snippet("while", "snippet", "while ${1:condition}:\n    ${0:pass}");
        add_snippet("enum", "snippet", "enum ${1:Name}:\n    ${0:VALUE}");
        add_snippet("import", "snippet", "import ${1:module}");
        add_snippet("from", "snippet", "from ${1:module} import ${2:symbol}");
        add_snippet("try", "snippet",
                    "try:\n    ${1:pass}\nexcept ${2:Exception} as ${3:error}:\n    ${0:pass}");
        add_snippet("except", "snippet", "except ${1:Exception} as ${2:error}:\n    ${0:pass}");
        for (std::string_view type : {"bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                                      "isize", "usize", "f32", "f64", "str", "cstr"}) {
            add(type, 25, "type");
        }
        if (doc != nullptr) {
            for (const auto& [name, type] : local_types_before_cursor(*doc, params)) {
                add(name, 6, name + ": " + type);
            }
            for (const Symbol& symbol : symbols_for_document(*doc)) {
                add(symbol.name, completion_kind(symbol.kind), symbol.detail);
            }
        }
        out << "]";
        return out.str();
    }

    std::string completion_resolve_result(const Json* params) const {
        const std::string label =
            params == nullptr ? std::string{} : string_value(params->get("label"));
        const int kind = int_value(params == nullptr ? nullptr : params->get("kind"));
        const std::string detail =
            params == nullptr ? std::string{} : string_value(params->get("detail"));
        std::ostringstream out;
        out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind;
        if (!detail.empty()) {
            out << ",\"detail\":\"" << json_escape(detail) << "\"";
        }
        if (const Json* insert_text = params == nullptr ? nullptr : params->get("insertText");
            insert_text != nullptr && insert_text->string() != nullptr) {
            out << ",\"insertText\":\"" << json_escape(*insert_text->string()) << "\"";
        }
        if (const int insert_format =
                int_value(params == nullptr ? nullptr : params->get("insertTextFormat"));
            insert_format != 0) {
            out << ",\"insertTextFormat\":" << insert_format;
        }
        out << ",\"documentation\":{\"kind\":\"markdown\",\"value\":\""
            << json_escape(completion_documentation(label, detail)) << "\"}}";
        return out.str();
    }

    std::string member_completion_result(const Document& doc, const std::string& target) const {
        if (const std::optional<std::string> module_result =
                module_completion_result(doc, target)) {
            return *module_result;
        }
        const std::string type = local_type_before_cursor(doc, target);
        if (type.empty()) {
            return "[]";
        }
        std::ostringstream out;
        out << "[";
        bool first = true;
        const auto add = [&](std::string_view label, int kind, std::string_view detail) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind
                << ",\"detail\":\"" << json_escape(detail) << "\"}";
        };
        try {
            ModuleAst module = parse_source(doc.text, doc.path);
            const ProjectConfig config = config_for_file(doc.path);
            merge_native_header_types(module,
                                      {.config = config, .source_dir = doc.path.parent_path()});
            const std::set<std::string> candidate_types = member_candidate_types(module, type);
            for (const ClassDecl& klass : module.classes) {
                if (!candidate_types.contains(klass.name)) {
                    continue;
                }
                for (const FieldDecl& field : klass.fields) {
                    add(field.name, 5, field.name + ": " + field.type);
                }
                for (const ConstDecl& constant : klass.constants) {
                    add(constant.name, 21, constant.name + ": " + constant.type);
                }
                for (const ConstDecl& field : klass.static_fields) {
                    add(field.name, 5, field.name + ": " + field.type);
                }
                for (const FunctionDecl& method : klass.methods) {
                    add(method.name, is_constructor_method_name(method.name) ? 4 : 2,
                        function_detail(method));
                }
                break;
            }
            for (const ClassDecl& klass : module.native_classes) {
                if (!candidate_types.contains(klass.name)) {
                    continue;
                }
                for (const FieldDecl& field : klass.fields) {
                    add(field.name, 5, field.name + ": " + field.type);
                }
                for (const FunctionDecl& method : klass.methods) {
                    add(method.name, is_constructor_method_name(method.name) ? 4 : 2,
                        function_detail(method));
                }
                break;
            }
        } catch (const std::exception&) {
        }
        out << "]";
        return out.str();
    }

    std::optional<std::string> module_completion_result(const Document& doc,
                                                        const std::string& target) const {
        ModuleAst module;
        try {
            module = parse_source(doc.text, doc.path);
        } catch (const std::exception&) {
            return std::nullopt;
        }
        for (const ImportDecl& import : module.imports) {
            if (import.kind != ImportKind::Module || bound_import_name(import) != target) {
                continue;
            }
            const std::filesystem::path file =
                module_path_to_file(doc.path.parent_path(), import.module_path);
            std::ifstream input(file);
            if (!input) {
                return "[]";
            }
            const std::string text{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
            const Document imported{
                .uri = file_uri(file),
                .path = file,
                .text = text,
            };
            std::ostringstream out;
            out << "[";
            bool first = true;
            for (const Symbol& symbol : symbols_for_document(imported, false)) {
                if (!first) {
                    out << ",";
                }
                first = false;
                out << "{\"label\":\"" << json_escape(symbol.name)
                    << "\",\"kind\":" << completion_kind(symbol.kind) << ",\"detail\":\""
                    << json_escape(symbol.detail) << "\"}";
            }
            out << "]";
            return out.str();
        }
        return std::nullopt;
    }

    static std::set<std::string> member_candidate_types(const ModuleAst& module,
                                                        const std::string& type) {
        std::set<std::string> out{type};
        bool changed = true;
        while (changed) {
            changed = false;
            for (const NativeTypeDecl& alias : module.native_types) {
                if (!alias.type.empty() && out.contains(alias.name) &&
                    out.insert(alias.type).second) {
                    changed = true;
                }
            }
            for (const TypeAliasDecl& alias : module.aliases) {
                if (!alias.type.empty() && out.contains(alias.name) &&
                    out.insert(alias.type).second) {
                    changed = true;
                }
            }
        }
        return out;
    }

    static int completion_kind(int symbol_kind) {
        switch (symbol_kind) {
        case 5:
            return 7;
        case 6:
            return 2;
        case 8:
            return 5;
        case 9:
            return 4;
        case 10:
            return 13;
        case 12:
            return 3;
        case 14:
            return 21;
        default:
            return 6;
        }
    }

    struct CallSite {
        std::string name;
        int parameter = 0;
    };

    std::string signature_help_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
        }
        const CallSite call = call_site_at(*doc, params);
        if (call.name.empty()) {
            return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
        }
        std::vector<std::string> signatures;
        for (const Symbol& symbol : symbols_for_document(*doc)) {
            if (symbol_matches(symbol.name, call.name) && (symbol.kind == 12 || symbol.kind == 6)) {
                signatures.push_back(symbol.detail);
            }
        }
        std::ostringstream out;
        out << "{\"signatures\":[";
        for (size_t i = 0; i < signatures.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            out << "{\"label\":\"" << json_escape(signatures[i]) << "\"}";
        }
        out << "],\"activeSignature\":0,\"activeParameter\":" << call.parameter << "}";
        return out.str();
    }

    CallSite call_site_at(const Document& doc, const Json* params) const {
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
            const int cursor = std::min(target_character, static_cast<int>(line.size()));
            int depth = 0;
            int parameter = 0;
            for (int i = cursor - 1; i >= 0; --i) {
                const char c = line[static_cast<size_t>(i)];
                if (c == ')') {
                    ++depth;
                } else if (c == '(') {
                    if (depth > 0) {
                        --depth;
                        continue;
                    }
                    int end = i;
                    while (end > 0 && std::isspace(static_cast<unsigned char>(
                                          line[static_cast<size_t>(end - 1)])) != 0) {
                        --end;
                    }
                    int start = end;
                    while (start > 0 && symbol_char(line[static_cast<size_t>(start - 1)])) {
                        --start;
                    }
                    return {
                        line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)),
                        parameter};
                } else if (c == ',' && depth == 0) {
                    ++parameter;
                }
            }
        }
        return {};
    }

    const Document* document_from_params(const Json* params) const {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        const std::string uri =
            text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
        const auto found = documents_.find(uri);
        return found == documents_.end() ? nullptr : &found->second;
    }

    std::optional<std::string> member_completion_target(const Document& doc,
                                                        const Json* params) const {
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
            while (end > 0 && std::isspace(static_cast<unsigned char>(
                                  line[static_cast<size_t>(end - 1)])) != 0) {
                --end;
            }
            int start = end;
            while (start > 0 && identifier_char(line[static_cast<size_t>(start - 1)])) {
                --start;
            }
            if (start == end) {
                return std::nullopt;
            }
            return line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
        }
        return std::nullopt;
    }

    static std::string local_type_before_cursor(const Document& doc, const std::string& name,
                                                const Json* params = nullptr) {
        const std::map<std::string, std::string> locals = local_types_before_cursor(doc, params);
        const auto found = locals.find(name);
        return found == locals.end() ? std::string{} : found->second;
    }

    static std::map<std::string, std::string> local_types_before_cursor(const Document& doc,
                                                                        const Json* params) {
        static const std::regex local_decl(
            R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_\.]*)\b)");
        static const std::regex assign_decl(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^=].*)$)");
        static const std::regex def_decl(R"(^\s*def\s+[A-Za-z_][A-Za-z0-9_]*\(([^)]*)\))");
        const Json* position = params == nullptr ? nullptr : params->get("position");
        const int max_line = position == nullptr ? std::numeric_limits<int>::max()
                                                 : int_value(position->get("line"));
        const int target_indent =
            params == nullptr ? std::numeric_limits<int>::max() : target_line_indent(doc, max_line);
        std::map<std::string, std::string> out;
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            if (row > max_line) {
                break;
            }
            std::smatch match;
            if (std::regex_search(line, match, def_decl)) {
                for (const std::string& param : split_top_level_args(match[1].str())) {
                    const size_t colon = param.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    const std::string name = trim_copy(param.substr(0, colon));
                    const std::string type = trim_copy(param.substr(colon + 1));
                    if (!name.empty() && !type.empty()) {
                        out[name] = type;
                    }
                }
            }
            if (std::regex_search(line, match, local_decl)) {
                if (leading_spaces(line) > target_indent) {
                    continue;
                }
                out[match[1].str()] = match[2].str();
            } else if (std::regex_search(line, match, assign_decl)) {
                if (leading_spaces(line) > target_indent) {
                    continue;
                }
                const std::string inferred = infer_simple_assignment_type(match[2].str());
                if (!inferred.empty()) {
                    out[match[1].str()] = inferred;
                }
            }
        }
        return out;
    }

    static std::string infer_simple_assignment_type(std::string expr) {
        expr = trim_copy(std::move(expr));
        if (expr == "True" || expr == "False") {
            return "bool";
        }
        if (expr.size() >= 2 && ((expr.front() == '"' && expr.back() == '"') ||
                                 (expr.front() == '\'' && expr.back() == '\''))) {
            return "str";
        }
        if (is_numeric_literal_text(expr)) {
            return expr.find('.') == std::string::npos ? "i32" : "f64";
        }
        const size_t call = expr.find('(');
        if (call != std::string::npos && call > 0 &&
            std::isupper(static_cast<unsigned char>(expr.front())) != 0) {
            return trim_copy(expr.substr(0, call));
        }
        return {};
    }

    static bool is_numeric_literal_text(const std::string& expr) {
        if (expr.empty()) {
            return false;
        }
        size_t start = (expr.front() == '-' || expr.front() == '+') ? 1 : 0;
        if (start == expr.size()) {
            return false;
        }
        auto valid_digit = [](char c, int base) {
            if (c == '_') {
                return true;
            }
            if (base <= 10) {
                return c >= '0' && c < static_cast<char>('0' + base);
            }
            return std::isdigit(static_cast<unsigned char>(c)) != 0 ||
                   (c >= 'a' && c < static_cast<char>('a' + base - 10)) ||
                   (c >= 'A' && c < static_cast<char>('A' + base - 10));
        };
        int base = 10;
        if (start + 1 < expr.size() && expr[start] == '0') {
            const char prefix = expr[start + 1];
            if (prefix == 'x' || prefix == 'X') {
                base = 16;
                start += 2;
            } else if (prefix == 'b' || prefix == 'B') {
                base = 2;
                start += 2;
            } else if (prefix == 'o' || prefix == 'O') {
                base = 8;
                start += 2;
            }
        }
        if (start == expr.size()) {
            return false;
        }
        bool digit = false;
        bool dot = false;
        for (size_t i = start; i < expr.size(); ++i) {
            const char c = expr[i];
            if (base != 10) {
                if (!valid_digit(c, base)) {
                    return false;
                }
                digit = digit || c != '_';
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                digit = true;
                continue;
            }
            if (c == '_') {
                continue;
            }
            if (c == '.' && !dot) {
                dot = true;
                continue;
            }
            return false;
        }
        return digit;
    }

    static int target_line_indent(const Document& doc, int target_line) {
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            if (row == target_line) {
                return leading_spaces(line);
            }
        }
        return std::numeric_limits<int>::max();
    }

    static std::string completion_documentation(const std::string& label,
                                                const std::string& detail) {
        if (detail == "snippet") {
            return "Dudu snippet for `" + label + "`.";
        }
        if (detail == "keyword") {
            return "Dudu keyword `" + label + "`.";
        }
        if (detail == "type") {
            return "Built-in Dudu type `" + label + "`.";
        }
        if (!detail.empty()) {
            return detail;
        }
        return label;
    }

};

} // namespace

int run_language_server(std::istream& in, std::ostream& out, std::ostream& err) {
    return LanguageServer(in, out, err).run();
}

} // namespace dudu
