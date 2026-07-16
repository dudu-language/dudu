#pragma once

#include "dudu/sema/sema_context.hpp"

#include <memory>
#include <vector>

namespace dudu {

class ProjectIndex;

struct LspPresentationSymbols {
    Symbols symbols;
    std::vector<std::unique_ptr<ClassDecl>> imported_classes;
};

LspPresentationSymbols presentation_symbols(const ProjectIndex& index, const ModuleAst& module);

} // namespace dudu
