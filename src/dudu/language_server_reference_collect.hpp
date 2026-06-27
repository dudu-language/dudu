#pragma once

#include "dudu/language_server_types.hpp"

#include <string>
#include <vector>

namespace dudu {

std::vector<ReferenceLocation> references_in(const Document& doc, const std::string& query);

} // namespace dudu
