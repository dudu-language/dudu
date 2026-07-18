#include "dudu/lsp/language_server.hpp"

#include "dudu/core/version.hpp"
#include "dudu/lsp/language_server_runtime.hpp"
#include "dudu/lsp/language_server_semantic_token_wire.hpp"
#include "dudu/lsp/language_server_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <ranges>

namespace dudu {

LanguageServer::LanguageServer(std::istream& in, std::ostream& out, std::ostream& err)
    : transport_(in, out), err_(err) {
    diagnostics_worker_ = std::thread([this] { diagnostics_worker_loop(); });
}

LanguageServer::~LanguageServer() {
    {
        const std::lock_guard lock(diagnostics_mutex_);
        diagnostics_stopping_ = true;
    }
    diagnostics_cv_.notify_one();
    if (diagnostics_worker_.joinable()) {
        diagnostics_worker_.join();
    }
}

int LanguageServer::run() {
    while (std::optional<std::string> body = transport_.read_message()) {
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

void LanguageServer::handle_message(const std::string& body) {
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
            configure_initialize(params);
            if (id != nullptr)
                transport_.respond(*id, initialize_result());
        } else if (method == "shutdown") {
            shutdown_ = true;
            if (id != nullptr)
                transport_.respond(*id, "null");
        } else if (method == "exit") {
            exit_ = true;
        } else if (method == "textDocument/didOpen") {
            did_open(params);
        } else if (method == "textDocument/didChange") {
            did_change(params);
        } else if (method == "textDocument/didSave") {
            did_save(params);
        } else if (method == "textDocument/didClose") {
            did_close(params);
        } else if (method == "workspace/didChangeWatchedFiles") {
            did_change_watched_files(params);
        } else if (method == "workspace/didChangeWorkspaceFolders") {
            did_change_workspace_folders(params);
        } else if (id != nullptr) {
            if (method == "textDocument/formatting")
                transport_.respond(*id, formatting_result(params));
            else if (method == "textDocument/documentSymbol")
                transport_.respond(*id, document_symbol_result(params));
            else if (method == "textDocument/semanticTokens/full")
                transport_.respond(*id, semantic_tokens_result(params));
            else if (method == "textDocument/definition")
                transport_.respond(*id, definition_result(params));
            else if (method == "textDocument/references")
                transport_.respond(*id, references_result(params));
            else if (method == "textDocument/prepareRename")
                transport_.respond(*id, prepare_rename_result(params));
            else if (method == "textDocument/rename")
                transport_.respond(*id, rename_result(params));
            else if (method == "textDocument/codeAction")
                transport_.respond(*id, code_action_result(params));
            else if (method == "textDocument/hover")
                transport_.respond(*id, hover_result(params));
            else if (method == "textDocument/inlayHint")
                transport_.respond(*id, inlay_hint_result(params));
            else if (method == "textDocument/completion")
                transport_.respond(*id, completion_result(params));
            else if (method == "completionItem/resolve")
                transport_.respond(*id, completion_resolve_result(params));
            else if (method == "textDocument/signatureHelp")
                transport_.respond(*id, signature_help_result(params));
            else if (method == "workspace/symbol")
                transport_.respond(*id, workspace_symbol_result(params));
            else if (method == "workspace/executeCommand")
                transport_.respond(*id, execute_command_result(params));
            else
                transport_.respond(*id, "null");
        }
    } catch (const std::exception& error) {
        if (id != nullptr)
            transport_.respond_error(*id, -32603, error.what());
        else
            err_ << "dudu-lsp: " << method << ": " << error.what() << '\n';
    }
    if (shutdown_ && method == "exit") {
        exit_ = true;
    }
}

std::string LanguageServer::initialize_result() {
    return std::string("{\"capabilities\":{") +
           "\"positionEncoding\":\"utf-16\","
           "\"textDocumentSync\":2,"
           "\"documentFormattingProvider\":true,"
           "\"documentSymbolProvider\":true,"
           "\"definitionProvider\":true,"
           "\"referencesProvider\":true,"
           "\"renameProvider\":{\"prepareProvider\":true},"
           "\"codeActionProvider\":true,"
           "\"semanticTokensProvider\":{\"legend\":" +
           std::string(semantic_token_legend_json()) +
           ",\"full\":true},"
           "\"hoverProvider\":true,"
           "\"inlayHintProvider\":{\"resolveProvider\":false},"
           "\"completionProvider\":{\"resolveProvider\":true,\"triggerCharacters\":[\".\"]},"
           "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\"[\",\",\"]},"
           "\"workspaceSymbolProvider\":true,"
           "\"executeCommandProvider\":{\"commands\":[\"dudu.showGeneratedCpp\"]},"
           "\"workspace\":{\"workspaceFolders\":{\"supported\":true,"
           "\"changeNotifications\":true}}"
           "},\"serverInfo\":{\"name\":\"dudu-lsp\",\"version\":\"" +
           std::string(kToolchainVersion) + "\"}}";
}

bool LanguageServer::bool_setting(const Json* object, std::string_view name, bool default_value) {
    const Json* value = object == nullptr ? nullptr : object->get(name);
    if (value == nullptr)
        return default_value;
    if (const bool* boolean = std::get_if<bool>(&value->value))
        return *boolean;
    return default_value;
}

void LanguageServer::configure_initialize(const Json* params) {
    workspace_roots_.clear();
    const auto add_workspace_uri = [&](const Json* value) {
        const std::string uri = string_value(value);
        if (uri.starts_with("file://"))
            workspace_roots_.emplace_back(file_uri_to_path(uri));
    };
    if (params != nullptr) {
        if (const JsonArray* folders = params->get("workspaceFolders") == nullptr
                                           ? nullptr
                                           : params->get("workspaceFolders")->array()) {
            for (const Json& folder : *folders)
                add_workspace_uri(folder.get("uri"));
        }
        add_workspace_uri(params->get("rootUri"));
        if (const std::string root_path = string_value(params->get("rootPath")); !root_path.empty())
            workspace_roots_.emplace_back(root_path);
    }

    const Json* init = params == nullptr ? nullptr : params->get("initializationOptions");
    const Json* hints = init == nullptr ? nullptr : init->get("inlayHints");
    inlay_hint_options_.inferred_types =
        bool_setting(hints, "inferredTypes", inlay_hint_options_.inferred_types);
    inlay_hint_options_.loop_binding_types =
        bool_setting(hints, "loopBindingTypes", inlay_hint_options_.loop_binding_types);
    inlay_hint_options_.implicit_self =
        bool_setting(hints, "implicitSelf", inlay_hint_options_.implicit_self);
    inlay_hint_options_.parameter_names =
        bool_setting(hints, "parameterNames", inlay_hint_options_.parameter_names);
    inlay_hint_options_.argument_types =
        bool_setting(hints, "argumentTypes", inlay_hint_options_.argument_types);
    workspace_cache_revision_ = std::numeric_limits<size_t>::max();
    workspace_cache_.clear();
}

int run_language_server(std::istream& in, std::ostream& out, std::ostream& err) {
    return LanguageServer(in, out, err).run();
}

} // namespace dudu
