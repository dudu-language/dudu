#pragma once

#include "dudu/language_server_types.hpp"

#include <string>

namespace dudu {

struct Json;

std::string definition_json(const Document& doc, const Json* params);

} // namespace dudu
