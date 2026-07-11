#pragma once

#include "dudu/core/token.hpp"
#include "dudu/parser/parse_result.hpp"

#include <filesystem>
#include <string_view>

namespace dudu {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<ParseDiagnostic> diagnostics;
};

std::vector<Token> lex_source(std::string_view source, const std::filesystem::path& file);
LexResult lex_source_recovering(std::string_view source, const std::filesystem::path& file);

} // namespace dudu
