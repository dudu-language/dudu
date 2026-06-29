#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/token.hpp"

#include <span>

namespace dudu {

ModuleAst parse_module(std::span<const Token> tokens);
ModuleAst parse_source(std::string_view source, const std::filesystem::path& file);

} // namespace dudu
