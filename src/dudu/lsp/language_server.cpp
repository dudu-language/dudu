#include "dudu/lsp/language_server.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/format/format.hpp"
#include "dudu/lsp/language_server_code_actions.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_diagnostics.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_semantic_tokens.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbol_results.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/lsp/language_server_types.hpp"
#include "dudu/lsp/language_server_workspace.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/sema/sema.hpp"

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
                err_ << "dudu-lsp: " << error.what() << '\n';
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
    size_t documents_revision_ = 0;
    mutable size_t workspace_cache_revision_ = std::numeric_limits<size_t>::max();
    mutable std::map<std::string, Document> workspace_cache_;
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

    void respond_error(const Json& id, int code, const std::string& message) {
        std::ostringstream body;
        body << "{\"jsonrpc\":\"2.0\",\"id\":" << id_json(id) << ",\"error\":{\"code\":" << code
             << ",\"message\":\"" << json_escape(message) << "\"}}";
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

        try {
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
            } else if (method == "textDocument/prepareRename") {
                if (id != nullptr) {
                    respond(*id, prepare_rename_result(params));
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
        } catch (const std::exception& error) {
            if (id != nullptr) {
                respond_error(*id, -32603, error.what());
            } else {
                err_ << "dudu-lsp: " << method << ": " << error.what() << '\n';
            }
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
               "\"renameProvider\":{\"prepareProvider\":true},"
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
               "},\"serverInfo\":{\"name\":\"dudu-lsp\",\"version\":\"0.1.0\"}}";
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
        invalidate_workspace_cache();
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
        invalidate_workspace_cache();
    }

    void did_save(const Json* params) {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        if (text_document == nullptr) {
            return;
        }
        invalidate_workspace_cache();
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
        try {
            const ProjectIndex& index = project_index_for_document(found->second, false);
            const ProjectIndex& native_index = project_index_for_document(found->second, true);
            return semantic_tokens_json(index, found->second.path, native_index);
        } catch (const std::exception&) {
            return "{\"data\":[]}";
        }
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
        if (documents_.empty()) {
            return "[]";
        }
        return workspace_symbols_json(query, documents_);
    }

    std::string definition_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        return definition_json(*doc, params, cached_workspace_documents());
    }

    std::string references_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "[]";
        }
        return references_json(*doc, params, cached_workspace_documents());
    }

    std::string rename_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        return rename_json(*doc, params, cached_workspace_documents());
    }

    std::string prepare_rename_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        return prepare_rename_json(*doc, params);
    }

    std::string code_action_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "[]";
        }
        return code_actions_json(*doc, params, cached_workspace_documents());
    }

    std::string hover_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        return hover_json(*doc, "", params, std::nullopt);
    }

    std::string completion_result(const Json* params) const {
        return completion_json(document_from_params(params), params);
    }

    std::string completion_resolve_result(const Json* params) const {
        return completion_resolve_json(params);
    }

    std::string signature_help_result(const Json* params) const {
        return signature_help_json(document_from_params(params), params);
    }

    const Document* document_from_params(const Json* params) const {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        const std::string uri =
            text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
        const auto found = documents_.find(uri);
        return found == documents_.end() ? nullptr : &found->second;
    }

    void invalidate_workspace_cache() {
        ++documents_revision_;
        workspace_cache_revision_ = std::numeric_limits<size_t>::max();
        workspace_cache_.clear();
        set_language_server_open_documents(documents_);
    }

    const std::map<std::string, Document>& cached_workspace_documents() const {
        if (workspace_cache_revision_ != documents_revision_) {
            workspace_cache_ = workspace_documents(documents_);
            workspace_cache_revision_ = documents_revision_;
        }
        return workspace_cache_;
    }
};

} // namespace

int run_language_server(std::istream& in, std::ostream& out, std::ostream& err) {
    return LanguageServer(in, out, err).run();
}

} // namespace dudu
