#pragma once

#include "dudu/project_config.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dudu {

std::string file_uri_to_path(std::string uri);
std::filesystem::path project_config_path(const std::filesystem::path& file);
ProjectConfig config_for_file(const std::filesystem::path& file);
int leading_spaces(const std::string& line);
int document_line_count(const std::string& text);
std::vector<std::string> document_lines(const std::string& text);

} // namespace dudu
