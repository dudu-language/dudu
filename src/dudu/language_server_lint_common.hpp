#pragma once

#include "dudu/language_server_types.hpp"

#include <filesystem>

namespace dudu {

bool lint_same_source_file(const std::filesystem::path& lhs, const std::filesystem::path& rhs);
SourceRange lint_delete_line_range(const SourceLocation& location, const Document& doc);

} // namespace dudu
