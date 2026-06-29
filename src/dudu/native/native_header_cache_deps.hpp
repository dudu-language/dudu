#pragma once

#include <filesystem>
#include <string>

namespace dudu {

std::string
native_header_dependency_stamps_from_makefile(const std::string& make_deps,
                                              const std::filesystem::path& generated_source = {});
bool native_header_dependency_stamps_current(const std::string& stamps);

} // namespace dudu
