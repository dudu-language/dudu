#include "dudu/language_server_workspace.hpp"

#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/parser.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dudu {
namespace {

void collect_imported_documents(const Document& doc,
                                const std::set<std::filesystem::path>& open_paths,
                                std::map<std::string, Document>& out,
                                std::set<std::filesystem::path>& visiting, size_t& scanned) {
    if (scanned >= 1000) {
        return;
    }
    ModuleAst module;
    try {
        module = parse_source(doc.text, doc.path);
    } catch (const std::exception&) {
        return;
    }
    for (const ImportDecl& import : module.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const std::filesystem::path path =
            module_path_to_file(doc.path.parent_path(), import.module_path);
        std::error_code error;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
        if (error || open_paths.contains(canonical) || visiting.contains(canonical)) {
            error.clear();
            continue;
        }
        std::ifstream file(path);
        if (!file) {
            continue;
        }
        const std::string text{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
        const std::string uri = file_uri(path);
        const Document imported{.uri = uri, .path = path, .text = text};
        out.try_emplace(uri, imported);
        ++scanned;
        visiting.insert(canonical);
        collect_imported_documents(imported, open_paths, out, visiting, scanned);
        visiting.erase(canonical);
        if (scanned >= 1000) {
            return;
        }
    }
}

void collect_workspace_documents(const std::filesystem::path& root,
                                 const std::set<std::filesystem::path>& open_paths,
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
        if (error || open_paths.contains(canonical)) {
            error.clear();
            continue;
        }
        std::ifstream file(path);
        if (!file) {
            continue;
        }
        const std::string text{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
        const std::string uri = file_uri(path);
        out[uri] = {.uri = uri, .path = path, .text = text};
        if (++scanned >= 1000) {
            return;
        }
    }
}

} // namespace

std::map<std::string, Document>
workspace_documents(const std::map<std::string, Document>& open_documents) {
    std::map<std::string, Document> out = open_documents;
    std::set<std::filesystem::path> open_paths;
    std::set<std::filesystem::path> roots;
    for (const auto& [uri, doc] : open_documents) {
        (void)uri;
        std::error_code error;
        open_paths.insert(std::filesystem::weakly_canonical(doc.path, error));
        const std::filesystem::path config = project_config_path(doc.path);
        roots.insert(config.empty() ? doc.path.parent_path() : config.parent_path());
    }
    size_t scanned = 0;
    std::set<std::filesystem::path> visiting;
    std::vector<Document> seed_documents;
    for (const auto& [uri, doc] : out) {
        (void)uri;
        seed_documents.push_back(doc);
    }
    for (const Document& doc : seed_documents) {
        collect_imported_documents(doc, open_paths, out, visiting, scanned);
    }
    for (const std::filesystem::path& root : roots) {
        collect_workspace_documents(root, open_paths, out, scanned);
    }
    return out;
}

} // namespace dudu
