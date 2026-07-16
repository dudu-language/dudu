#include "dudu/lsp/language_server_workspace.hpp"

#include "dudu/core/file_io.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/project/project_index.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dudu {
namespace {

std::filesystem::path canonical_workspace_path(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

void collect_indexed_documents(const Document& doc,
                               const std::set<std::filesystem::path>& open_paths,
                               std::set<std::filesystem::path>& indexed_paths,
                               std::map<std::string, Document>& out) {
    const ProjectIndex* index = nullptr;
    try {
        index = &project_index_for_document(doc, false);
    } catch (const std::exception&) {
        return;
    }
    for (const ProjectModuleSummary& module : index->modules()) {
        if (module.source_path.empty()) {
            return;
        }
        const std::filesystem::path canonical = canonical_workspace_path(module.source_path);
        indexed_paths.insert(canonical);
        if (open_paths.contains(canonical)) {
            continue;
        }
        const std::string uri = file_uri(module.source_path);
        if (out.contains(uri)) {
            continue;
        }
        std::optional<std::string> text = try_read_text_file(module.source_path);
        if (!text) {
            continue;
        }
        const Document imported{.uri = uri, .path = module.source_path, .text = std::move(*text)};
        out.try_emplace(uri, imported);
    }
}

void collect_workspace_documents(const std::filesystem::path& root,
                                 const std::set<std::filesystem::path>& open_paths,
                                 const std::set<std::filesystem::path>& indexed_paths,
                                 std::map<std::string, Document>& out, size_t& scanned) {
    std::error_code error;
    if (root.empty() || !std::filesystem::exists(root, error) || error) {
        return;
    }
    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        const std::filesystem::directory_entry& entry = *it;
        const std::filesystem::path path = entry.path();
        if (entry.is_directory(error) && skip_workspace_dir(path.filename().string())) {
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(error) || path.extension() != ".dd") {
            continue;
        }
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
        if (error || open_paths.contains(canonical) || indexed_paths.contains(canonical)) {
            error.clear();
            continue;
        }
        std::optional<std::string> text = try_read_text_file(path);
        if (!text) {
            continue;
        }
        const std::string uri = file_uri(path);
        out[uri] = {.uri = uri, .path = path, .text = std::move(*text)};
        if (++scanned >= 1000) {
            return;
        }
    }
}

} // namespace

std::map<std::string, Document> workspace_documents(
    const std::map<std::string, Document>& open_documents,
    const std::vector<std::filesystem::path>& workspace_roots) {
    std::map<std::string, Document> out = open_documents;
    std::set<std::filesystem::path> open_paths;
    std::set<std::filesystem::path> indexed_paths;
    std::set<std::filesystem::path> roots;
    for (const std::filesystem::path& root : workspace_roots) {
        if (!root.empty()) {
            roots.insert(canonical_workspace_path(root));
        }
    }
    for (const auto& [uri, doc] : open_documents) {
        (void)uri;
        open_paths.insert(canonical_workspace_path(doc.path));
        const std::filesystem::path config = project_config_path(doc.path);
        if (!config.empty()) {
            roots.insert(canonical_workspace_path(config.parent_path()));
        }
    }
    size_t scanned = 0;
    std::vector<Document> seed_documents;
    for (const auto& [uri, doc] : out) {
        (void)uri;
        seed_documents.push_back(doc);
    }
    for (const Document& doc : seed_documents) {
        collect_indexed_documents(doc, open_paths, indexed_paths, out);
    }
    for (const std::filesystem::path& root : roots) {
        collect_workspace_documents(root, open_paths, indexed_paths, out, scanned);
    }
    return out;
}

} // namespace dudu
