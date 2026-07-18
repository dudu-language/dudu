#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/project/project_index_cache.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dudu {

std::string file_uri_to_path(std::string uri);
std::filesystem::path project_config_path(const std::filesystem::path& file);
ProjectConfig config_for_file(const std::filesystem::path& file);
using ProjectIndexSnapshot = std::shared_ptr<const ProjectIndex>;
ProjectIndexSnapshot project_index_for_document(const Document& doc, bool include_native_headers,
                                                bool check_semantics = false,
                                                bool allow_last_good = true);
ProjectIndexOptions project_index_options_for_document(
    const Document& doc, bool include_native_headers, bool check_semantics,
    const std::map<std::filesystem::path, std::string>& source_overrides = {});
ProjectIndexCacheStats language_server_project_index_cache_stats();
void set_language_server_open_documents(const std::map<std::string, Document>& documents);
void clear_language_server_module_cache();
int leading_spaces(const std::string& line);
int document_line_count(const std::string& text);
std::vector<std::string> document_lines(const std::string& text);

} // namespace dudu
