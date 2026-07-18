#pragma once

#include "dudu/lsp/language_server_diagnostics.hpp"
#include "dudu/lsp/language_server_inlay_hints.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_transport.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <condition_variable>
#include <filesystem>
#include <iosfwd>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dudu {

class LanguageServer {
  public:
    LanguageServer(std::istream& in, std::ostream& out, std::ostream& err);
    ~LanguageServer();

    int run();

  private:
    struct DiagnosticsJob {
        Document document;
        std::map<std::filesystem::path, std::string> source_overrides;
        size_t revision = 0;
    };

    LspTransport transport_;
    std::ostream& err_;
    std::map<std::string, Document> documents_;
    size_t documents_revision_ = 0;
    std::map<std::string, size_t> document_revisions_;
    mutable size_t workspace_cache_revision_ = std::numeric_limits<size_t>::max();
    mutable std::map<std::string, Document> workspace_cache_;
    std::vector<std::filesystem::path> workspace_roots_;
    InlayHintOptions inlay_hint_options_{};
    bool shutdown_ = false;
    bool exit_ = false;
    mutable std::mutex diagnostics_mutex_;
    std::condition_variable diagnostics_cv_;
    std::map<std::string, DiagnosticsJob> pending_diagnostics_;
    std::map<std::string, size_t> native_ready_revisions_;
    bool diagnostics_stopping_ = false;
    std::thread diagnostics_worker_;

    void handle_message(const std::string& body);
    static std::string initialize_result();
    static bool bool_setting(const Json* object, std::string_view name, bool default_value);
    void configure_initialize(const Json* params);

    void did_open(const Json* params);
    void did_change(const Json* params);
    void did_close(const Json* params);
    void did_save(const Json* params);
    void did_change_watched_files(const Json* params);
    void did_change_workspace_folders(const Json* params);
    void publish_syntax_diagnostics(const std::string& uri);
    void publish_diagnostics(const std::string& uri,
                             const std::vector<Diagnostic>& diagnostics,
                             std::optional<int> version);
    void queue_full_diagnostics(const std::string& uri);
    void queue_full_diagnostics(const std::set<std::string>& uris);
    void diagnostics_worker_loop();
    bool full_diagnostics_ready(const std::string& uri) const;

    std::string semantic_tokens_result(const Json* params) const;
    std::string formatting_result(const Json* params) const;
    static int line_count(const std::string& text);
    std::string document_symbol_result(const Json* params) const;
    std::string workspace_symbol_result(const Json* params) const;
    std::string definition_result(const Json* params) const;
    std::string references_result(const Json* params) const;
    std::string rename_result(const Json* params) const;
    std::string prepare_rename_result(const Json* params) const;
    std::string code_action_result(const Json* params) const;
    std::string hover_result(const Json* params) const;
    std::string completion_result(const Json* params) const;
    std::string inlay_hint_result(const Json* params) const;
    std::string completion_resolve_result(const Json* params) const;
    std::string signature_help_result(const Json* params) const;
    const Document* document_from_params(const Json* params) const;

    void invalidate_workspace_cache(const std::string& uri);
    void invalidate_workspace_cache(const std::set<std::string>& uris);
    std::set<std::string> all_open_document_uris() const;
    void invalidate_all_open_documents();
    std::set<std::string> affected_open_documents(const std::filesystem::path& changed_path,
                                                  const std::string& changed_uri) const;
    const std::map<std::string, Document>& cached_workspace_documents() const;
};

} // namespace dudu
