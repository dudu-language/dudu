#pragma once

#include "dudu/core/token.hpp"

#include <filesystem>
#include <string_view>

namespace dudu {

std::vector<Token> lex_source(std::string_view source, const std::filesystem::path& file);

} // namespace dudu
