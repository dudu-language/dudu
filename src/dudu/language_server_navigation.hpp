#pragma once

#include "dudu/language_server_types.hpp"
#include "dudu/source.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dudu {

struct Json;

std::string range_json(const SourceLocation& location);
std::string range_json(int line, int start_character, int end_character);
std::string range_json(int start_line, int start_character, int end_line, int end_character);

std::string location_json(const std::string& uri, const std::string& range);
std::string uri_for_location(const SourceLocation& location, const Document& doc);
std::string file_uri(const std::filesystem::path& path);

std::string symbol_at(const Document& doc, const Json* params);
bool symbol_matches(const std::string& symbol, const std::string& query);
bool symbol_char(char c);
bool identifier_char(char c);
bool valid_identifier(const std::string& value);

std::vector<ReferenceLocation> references_in(const Document& doc, const std::string& query);

bool same_path(const std::filesystem::path& lhs, const std::filesystem::path& rhs);
bool skip_workspace_dir(const std::string& name);
std::filesystem::path module_path_to_file(const std::filesystem::path& base,
                                          const std::string& module_path);

std::string lower_copy(std::string value);

} // namespace dudu
