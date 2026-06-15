#pragma once

#include "dudu/language_server_types.hpp"

#include <map>
#include <string>

namespace dudu {

struct Json;

std::string code_actions_json(const Document& doc, const Json* params,
                              const std::map<std::string, Document>& workspace);

} // namespace dudu
