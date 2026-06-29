#pragma once

#include "dudu/core/ast.hpp"

#include <optional>
#include <string>

namespace dudu {

struct NativeClassDefinition {
    std::string name;
    SourceLocation location;
};

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const ModuleAst& module, const NativeTypeDecl& alias);

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const ModuleAst& module, const std::string& alias_name);

} // namespace dudu
