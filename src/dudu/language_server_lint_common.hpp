#pragma once

#include <filesystem>

namespace dudu {

bool lint_same_source_file(const std::filesystem::path& lhs, const std::filesystem::path& rhs);

} // namespace dudu
