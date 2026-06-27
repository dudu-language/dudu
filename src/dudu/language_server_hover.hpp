#pragma once

#include "dudu/ast_expr.hpp"
#include "dudu/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

struct Json;

std::string hover_json(const Document& doc, const std::string& word, const std::string& local_type,
                       const Json* params = nullptr,
                       std::optional<ExprPath> selected_path = std::nullopt);

} // namespace dudu
