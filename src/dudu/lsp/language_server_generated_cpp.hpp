#pragma once

#include <string>

namespace dudu {

struct Document;
struct Json;
class ProjectIndex;

std::string generated_cpp_json(const Document& document, const Json* command_argument,
                               const ProjectIndex& index);

} // namespace dudu
