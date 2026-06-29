#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dudu {

std::filesystem::path module_path_to_file(const std::filesystem::path& base,
                                          const std::string& module_path);
std::string module_name_from_file(const std::filesystem::path& root,
                                  const std::filesystem::path& file);
std::string module_path_for_cycle(const std::filesystem::path& root,
                                  const std::filesystem::path& file);
std::string module_cycle_message(const std::filesystem::path& root,
                                 const std::vector<std::filesystem::path>& stack,
                                 const std::filesystem::path& repeated);
std::string generated_type_name(const std::string& module_path, const std::string& name);
std::string generated_value_name(const std::string& module_path, const std::string& name);

} // namespace dudu
