#pragma once

#include "dudu/ast.hpp"
#include "dudu/language_server_types.hpp"
#include "dudu/project_index.hpp"
#include "dudu/project_config.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dudu {

std::string file_uri_to_path(std::string uri);
std::filesystem::path project_config_path(const std::filesystem::path& file);
ProjectConfig config_for_file(const std::filesystem::path& file);
const ProjectIndex& project_index_for_document(const Document& doc, bool include_native_headers,
                                               bool check_semantics = false);
void clear_language_server_module_cache();
int leading_spaces(const std::string& line);
int document_line_count(const std::string& text);
std::vector<std::string> document_lines(const std::string& text);

} // namespace dudu
