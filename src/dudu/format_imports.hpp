#pragma once

#include "dudu/ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct OrganizedImportBlock {
    size_t start_line = 0;
    size_t end_line = 0;
    std::vector<std::string> lines;
    std::string replacement_text;
};

std::optional<OrganizedImportBlock>
organized_leading_import_block(const std::vector<std::string>& source_lines,
                               const ModuleAst& module);

} // namespace dudu
