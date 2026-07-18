#include "dudu/lsp/language_server_runtime.hpp"

#include "dudu/format/format.hpp"
#include "dudu/lsp/language_server_code_actions.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_semantic_tokens.hpp"
#include "dudu/lsp/language_server_signature_help.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbol_results.hpp"
#include "dudu/parser/parser.hpp"

#include <sstream>

namespace dudu {

std::string LanguageServer::semantic_tokens_result(const Json* params) const {
    const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
    const std::string uri =
        text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
    const auto found = documents_.find(uri);
    if (found == documents_.end())
        return "{\"data\":[]}";
    try {
        const ProjectIndexSnapshot index =
            project_index_for_document(found->second, false, false, false);
        if (full_diagnostics_ready(uri)) {
            const ProjectIndexSnapshot native_index =
                project_index_for_document(found->second, true, false, false);
            return semantic_tokens_json(*index, found->second.path, *native_index);
        }
        return semantic_tokens_json(*index, found->second.path, *index);
    } catch (const std::exception&) {
        const ParseResult recovered =
            parse_source_recovering(found->second.text, found->second.path);
        if (!recovered.module.imports.empty() || !recovered.module.aliases.empty() ||
            !recovered.module.enums.empty() || !recovered.module.classes.empty() ||
            !recovered.module.constants.empty() || !recovered.module.functions.empty())
            return semantic_tokens_json(recovered.module);
        return lexical_semantic_tokens_json(found->second.text);
    }
}

std::string LanguageServer::formatting_result(const Json* params) const {
    const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
    const std::string uri =
        text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
    const auto found = documents_.find(uri);
    if (found == documents_.end())
        return "[]";
    const std::string formatted = format_source(found->second.text);
    std::ostringstream out;
    out << "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":"
        << line_count(found->second.text) << ",\"character\":0}},\"newText\":\""
        << json_escape(formatted) << "\"}]";
    return out.str();
}

int LanguageServer::line_count(const std::string& text) {
    int lines = 0;
    for (const char c : text) {
        if (c == '\n')
            ++lines;
    }
    return lines + 1;
}

std::string LanguageServer::document_symbol_result(const Json* params) const {
    const Document* doc = document_from_params(params);
    return doc == nullptr ? "[]" : document_symbols_json(*doc);
}

std::string LanguageServer::workspace_symbol_result(const Json* params) const {
    const std::string query =
        params == nullptr ? std::string{} : string_value(params->get("query"));
    if (documents_.empty())
        return "[]";
    return workspace_symbols_json(query, cached_workspace_documents());
}

std::string LanguageServer::definition_result(const Json* params) const {
    const Document* doc = document_from_params(params);
    return doc == nullptr ? "null"
                          : definition_json(*doc, params, cached_workspace_documents());
}

std::string LanguageServer::references_result(const Json* params) const {
    const Document* doc = document_from_params(params);
    return doc == nullptr ? "[]"
                          : references_json(*doc, params, cached_workspace_documents());
}

std::string LanguageServer::rename_result(const Json* params) const {
    const Document* doc = document_from_params(params);
    return doc == nullptr ? "null" : rename_json(*doc, params, cached_workspace_documents());
}

std::string LanguageServer::prepare_rename_result(const Json* params) const {
    const Document* doc = document_from_params(params);
    return doc == nullptr ? "null" : prepare_rename_json(*doc, params);
}

std::string LanguageServer::code_action_result(const Json* params) const {
    const Document* doc = document_from_params(params);
    return doc == nullptr ? "[]"
                          : code_actions_json(*doc, params, cached_workspace_documents());
}

std::string LanguageServer::hover_result(const Json* params) const {
    const Document* doc = document_from_params(params);
    return doc == nullptr ? "null" : hover_json(*doc, "", params, std::nullopt);
}

std::string LanguageServer::completion_result(const Json* params) const {
    return completion_json(document_from_params(params), params);
}

std::string LanguageServer::inlay_hint_result(const Json* params) const {
    const Document* doc = document_from_params(params);
    return doc == nullptr ? "[]" : inlay_hints_json(*doc, params, inlay_hint_options_);
}

std::string LanguageServer::completion_resolve_result(const Json* params) const {
    return completion_resolve_json(params);
}

std::string LanguageServer::signature_help_result(const Json* params) const {
    return signature_help_json(document_from_params(params), params);
}

const Document* LanguageServer::document_from_params(const Json* params) const {
    const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
    const std::string uri =
        text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
    const auto found = documents_.find(uri);
    return found == documents_.end() ? nullptr : &found->second;
}

} // namespace dudu
