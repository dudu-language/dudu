#pragma once

#include "dudu/language_server_types.hpp"

#include <string>

namespace dudu {

struct Json;

std::string hover_json(const Document& doc, const std::string& word,
                       const std::string& local_type, const Json* params = nullptr);

} // namespace dudu
