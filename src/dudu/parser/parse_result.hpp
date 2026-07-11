#pragma once

#include "dudu/core/ast.hpp"

#include <string>
#include <vector>

namespace dudu {

struct ParseDiagnostic {
    SourceLocation location;
    std::string message;
    std::string code;
    std::string data_name;
};

struct ParseResult {
    ModuleAst module;
    std::vector<ParseDiagnostic> diagnostics;
};

} // namespace dudu
