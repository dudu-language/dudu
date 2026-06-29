#include "dudu/lsp/language_server_support.hpp"

#include "dudu/project/project_index_cache.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>

namespace dudu {
namespace {

ProjectIndexCache project_index_cache;
std::map<std::filesystem::path, std::string> open_document_sources;

std::filesystem::path canonical_overlay_path(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

} // namespace

std::string file_uri_to_path(std::string uri) {
    constexpr std::string_view prefix = "file://";
    if (uri.rfind(prefix, 0) == 0) {
        uri.erase(0, prefix.size());
    }
    std::string out;
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const std::string hex = uri.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(uri[i]);
        }
    }
    return out;
}

std::filesystem::path project_config_path(const std::filesystem::path& file) {
    std::filesystem::path dir = file.has_parent_path() ? file.parent_path() : ".";
    while (true) {
        const std::filesystem::path candidate = dir / "dudu.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir == dir.parent_path()) {
            return {};
        }
        dir = dir.parent_path();
    }
}

ProjectConfig config_for_file(const std::filesystem::path& file) {
    const std::filesystem::path config = project_config_path(file);
    if (config.empty()) {
        return {};
    }
    ProjectConfig parsed = parse_project_config(config);
    auto absolutize = [&](std::vector<std::string>& paths) {
        for (std::string& path_text : paths) {
            path_text = project_path(parsed, path_text).string();
        }
    };
    absolutize(parsed.include_dirs);
    absolutize(parsed.lib_dirs);
    return parsed;
}

const ProjectIndex& project_index_for_document(const Document& doc, bool include_native_headers,
                                               bool check_semantics) {
    const ProjectConfig config = config_for_file(doc.path);
    std::map<std::filesystem::path, std::string> source_overrides = open_document_sources;
    source_overrides[canonical_overlay_path(doc.path)] = doc.text;
    ProjectIndexOptions options;
    options.entry_path = doc.path;
    options.entry_source = doc.text;
    options.source_overrides = std::move(source_overrides);
    options.config = config;
    options.source_dir = doc.path.parent_path();
    options.allow_module_tree = doc.path.has_parent_path() &&
                                std::filesystem::exists(doc.path.parent_path());
    options.include_native_headers = include_native_headers;
    options.include_native_headers_in_merged_module = include_native_headers;
    options.check_semantics = check_semantics;
    options.semantic_options = {.check_bodies = true};
    return project_index_cache.get(options);
}

ProjectIndexCacheStats language_server_project_index_cache_stats() {
    return project_index_cache.stats();
}

void set_language_server_open_documents(const std::map<std::string, Document>& documents) {
    open_document_sources.clear();
    for (const auto& [uri, doc] : documents) {
        (void)uri;
        open_document_sources[canonical_overlay_path(doc.path)] = doc.text;
    }
}

void clear_language_server_module_cache() {
    project_index_cache.clear();
    open_document_sources.clear();
}

int leading_spaces(const std::string& line) {
    int out = 0;
    while (out < static_cast<int>(line.size()) && line[static_cast<size_t>(out)] == ' ') {
        ++out;
    }
    return out;
}

int document_line_count(const std::string& text) {
    return static_cast<int>(std::count(text.begin(), text.end(), '\n')) +
           (text.empty() || text.back() == '\n' ? 0 : 1);
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

} // namespace dudu
