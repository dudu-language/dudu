#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

enum class GenericParamOwnerKind {
    Function,
    Class,
    Method,
};

struct GenericParamTarget {
    std::string name;
    SourceLocation declaration;
    SourceLocation owner;
    GenericParamOwnerKind owner_kind = GenericParamOwnerKind::Function;
    bool value = false;
};

std::optional<GenericParamTarget>
generic_param_target_at(const ModuleAst& module, LspPosition position, const std::string& name);
std::vector<ReferenceLocation> generic_param_references(const ModuleAst& module,
                                                        const Document& doc,
                                                        const GenericParamTarget& target);

} // namespace dudu
