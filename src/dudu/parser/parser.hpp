#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/token.hpp"
#include "dudu/parser/parse_result.hpp"

#include <span>

namespace dudu {

ModuleAst parse_module(std::span<const Token> tokens);
ModuleAst parse_source(std::string_view source, const std::filesystem::path& file);
ParseResult parse_module_recovering(std::span<const Token> tokens);
ParseResult parse_source_recovering(std::string_view source, const std::filesystem::path& file);

} // namespace dudu
