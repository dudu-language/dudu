#include "dudu/lsp/language_server_runtime.hpp"

#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_text_sync.hpp"
#include "dudu/lsp/language_server_workspace.hpp"

#include <algorithm>
#include <ranges>
#include <sstream>

namespace dudu {

void LanguageServer::did_open(const Json* params) {
    const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
    if (text_document == nullptr)
        return;
    const std::string uri = string_value(text_document->get("uri"));
    documents_[uri] = {.uri = uri,
                       .path = file_uri_to_path(uri),
                       .text = string_value(text_document->get("text")),
                       .version = optional_int_value(text_document->get("version"))};
    invalidate_workspace_cache(uri);
    std::set<std::string> affected = affected_open_documents(documents_[uri].path, uri);
    affected.erase(uri);
    invalidate_workspace_cache(affected);
    affected.insert(uri);
    publish_syntax_diagnostics(uri);
    queue_full_diagnostics(affected);
}

void LanguageServer::did_change(const Json* params) {
    const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
    const Json* changes = params == nullptr ? nullptr : params->get("contentChanges");
    const JsonArray* array = changes == nullptr ? nullptr : changes->array();
    if (text_document == nullptr || array == nullptr || array->empty())
        return;
    const std::string uri = string_value(text_document->get("uri"));
    const auto found = documents_.find(uri);
    if (found == documents_.end())
        return;
    Document& doc = found->second;
    const int version = optional_int_value(text_document->get("version"), doc.version + 1);
    if (version <= doc.version)
        return;
    apply_lsp_content_changes(doc.text, *array);
    doc.version = version;
    invalidate_workspace_cache(uri);
    std::set<std::string> affected = affected_open_documents(doc.path, uri);
    affected.erase(uri);
    invalidate_workspace_cache(affected);
    affected.insert(uri);
    publish_syntax_diagnostics(uri);
    queue_full_diagnostics(affected);
}

void LanguageServer::did_close(const Json* params) {
    const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
    if (text_document == nullptr)
        return;
    const std::string uri = string_value(text_document->get("uri"));
    const auto document = documents_.find(uri);
    if (document == documents_.end())
        return;
    const std::filesystem::path path = document->second.path;
    {
        const std::lock_guard lock(diagnostics_mutex_);
        pending_diagnostics_.erase(uri);
        native_ready_revisions_.erase(uri);
    }
    documents_.erase(uri);
    invalidate_workspace_cache(uri);
    const std::set<std::string> affected = affected_open_documents(path, {});
    invalidate_workspace_cache(affected);
    publish_diagnostics(uri, {}, std::nullopt);
    queue_full_diagnostics(affected);
}

void LanguageServer::did_save(const Json* params) {
    const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
    if (text_document == nullptr)
        return;
    const std::string uri = string_value(text_document->get("uri"));
    invalidate_workspace_cache(uri);
    publish_syntax_diagnostics(uri);
    queue_full_diagnostics(uri);
}

void LanguageServer::did_change_watched_files(const Json* params) {
    const Json* changes = params == nullptr ? nullptr : params->get("changes");
    const JsonArray* array = changes == nullptr ? nullptr : changes->array();
    if (array == nullptr || array->empty())
        return;
    invalidate_all_open_documents();
    queue_full_diagnostics(all_open_document_uris());
}

void LanguageServer::did_change_workspace_folders(const Json* params) {
    const Json* event = params == nullptr ? nullptr : params->get("event");
    if (event == nullptr)
        return;
    const auto workspace_path = [](const Json& folder) -> std::filesystem::path {
        const std::string uri = string_value(folder.get("uri"));
        return uri.starts_with("file://") ? std::filesystem::path(file_uri_to_path(uri))
                                           : std::filesystem::path{};
    };
    if (const JsonArray* removed = event->get("removed") == nullptr
                                       ? nullptr
                                       : event->get("removed")->array()) {
        for (const Json& folder : *removed)
            std::erase(workspace_roots_, workspace_path(folder));
    }
    if (const JsonArray* added = event->get("added") == nullptr
                                     ? nullptr
                                     : event->get("added")->array()) {
        for (const Json& folder : *added) {
            const std::filesystem::path path = workspace_path(folder);
            if (!path.empty() &&
                std::ranges::find(workspace_roots_, path) == workspace_roots_.end())
                workspace_roots_.push_back(path);
        }
    }
    invalidate_all_open_documents();
    queue_full_diagnostics(all_open_document_uris());
}

void LanguageServer::publish_syntax_diagnostics(const std::string& uri) {
    const auto found = documents_.find(uri);
    if (found == documents_.end())
        return;
    publish_diagnostics(uri, syntax_diagnostics_for_document(found->second),
                        found->second.version);
}

void LanguageServer::publish_diagnostics(const std::string& uri,
                                         const std::vector<Diagnostic>& diagnostics,
                                         std::optional<int> version) {
    std::ostringstream params;
    params << "{\"uri\":\"" << json_escape(uri) << "\"";
    if (version.has_value())
        params << ",\"version\":" << *version;
    params << ",\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0)
            params << ',';
        params << diagnostic_json(diagnostics[i]);
    }
    params << "]}";
    transport_.notify("textDocument/publishDiagnostics", params.str());
}

void LanguageServer::queue_full_diagnostics(const std::string& uri) {
    const auto found = documents_.find(uri);
    if (found == documents_.end())
        return;
    size_t revision = 0;
    {
        const std::lock_guard lock(diagnostics_mutex_);
        revision = document_revisions_[uri];
    }
    DiagnosticsJob job{.document = found->second,
                       .source_overrides = {},
                       .revision = revision};
    for (const auto& [_, document] : documents_)
        job.source_overrides[document.path] = document.text;
    {
        const std::lock_guard lock(diagnostics_mutex_);
        pending_diagnostics_.insert_or_assign(uri, std::move(job));
    }
    diagnostics_cv_.notify_one();
}

void LanguageServer::queue_full_diagnostics(const std::set<std::string>& uris) {
    for (const std::string& uri : uris)
        queue_full_diagnostics(uri);
}

void LanguageServer::diagnostics_worker_loop() {
    while (true) {
        DiagnosticsJob job;
        {
            std::unique_lock lock(diagnostics_mutex_);
            diagnostics_cv_.wait(
                lock, [&] { return diagnostics_stopping_ || !pending_diagnostics_.empty(); });
            if (diagnostics_stopping_ && pending_diagnostics_.empty())
                return;
            auto next = pending_diagnostics_.begin();
            job = std::move(next->second);
            pending_diagnostics_.erase(next);
        }
        const std::vector<Diagnostic> diagnostics =
            diagnostics_for_document_snapshot(job.document, job.source_overrides);
        const std::lock_guard lock(diagnostics_mutex_);
        const auto revision = document_revisions_.find(job.document.uri);
        if (revision != document_revisions_.end() && job.revision == revision->second) {
            native_ready_revisions_[job.document.uri] = job.revision;
            publish_diagnostics(job.document.uri, diagnostics, job.document.version);
            transport_.request_client_refresh("workspace/semanticTokens/refresh");
            transport_.request_client_refresh("workspace/inlayHint/refresh");
        }
    }
}

bool LanguageServer::full_diagnostics_ready(const std::string& uri) const {
    const std::lock_guard lock(diagnostics_mutex_);
    const auto ready = native_ready_revisions_.find(uri);
    const auto current = document_revisions_.find(uri);
    return ready != native_ready_revisions_.end() && current != document_revisions_.end() &&
           ready->second == current->second;
}

void LanguageServer::invalidate_workspace_cache(const std::string& uri) {
    {
        const std::lock_guard lock(diagnostics_mutex_);
        ++documents_revision_;
        ++document_revisions_[uri];
    }
    workspace_cache_revision_ = std::numeric_limits<size_t>::max();
    workspace_cache_.clear();
    set_language_server_open_documents(documents_);
}

void LanguageServer::invalidate_workspace_cache(const std::set<std::string>& uris) {
    for (const std::string& uri : uris) {
        if (documents_.contains(uri))
            invalidate_workspace_cache(uri);
    }
}

std::set<std::string> LanguageServer::all_open_document_uris() const {
    std::set<std::string> uris;
    for (const auto& [uri, _] : documents_)
        uris.insert(uri);
    return uris;
}

void LanguageServer::invalidate_all_open_documents() {
    {
        const std::lock_guard lock(diagnostics_mutex_);
        ++documents_revision_;
        for (const auto& [uri, _] : documents_)
            ++document_revisions_[uri];
        native_ready_revisions_.clear();
    }
    workspace_cache_revision_ = std::numeric_limits<size_t>::max();
    workspace_cache_.clear();
    clear_language_server_module_cache();
    set_language_server_open_documents(documents_);
}

std::set<std::string>
LanguageServer::affected_open_documents(const std::filesystem::path& changed_path,
                                        const std::string& changed_uri) const {
    std::set<std::string> affected;
    if (!changed_uri.empty() && documents_.contains(changed_uri))
        affected.insert(changed_uri);
    for (const auto& [uri, document] : documents_) {
        try {
            const ProjectIndexSnapshot index = project_index_for_document(document, false);
            const ProjectModuleSummary* changed = index->summary_for_path(changed_path);
            const ProjectModuleSummary* current = index->summary_for_path(document.path);
            if (changed == nullptr || current == nullptr)
                continue;
            const std::vector<std::string> modules =
                index->affected_modules_for_sources({changed_path});
            if (std::ranges::find(modules, current->module_path) != modules.end())
                affected.insert(uri);
        } catch (const std::exception&) {
        }
    }
    return affected;
}

const std::map<std::string, Document>& LanguageServer::cached_workspace_documents() const {
    if (workspace_cache_revision_ != documents_revision_) {
        workspace_cache_ = workspace_documents(documents_, workspace_roots_);
        workspace_cache_revision_ = documents_revision_;
    }
    return workspace_cache_;
}

} // namespace dudu
