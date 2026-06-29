#pragma once

#include "dudu/core/ast.hpp"

#include <map>
#include <optional>
#include <string>

namespace dudu {

struct NativeClassDefinition {
    std::string name;
    SourceLocation location;
    std::string doc_comment;
};

struct NativeClassDefinitionIndex {
    std::map<std::string, NativeClassDefinition> by_name;
    std::map<std::string, NativeClassDefinition> by_identity;
};

NativeClassDefinitionIndex native_class_definition_index(const ModuleAst& module);

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const ModuleAst& module, const NativeTypeDecl& alias);

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const NativeClassDefinitionIndex& class_index,
                                     const NativeTypeDecl& alias);

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const ModuleAst& module, const std::string& alias_name);

} // namespace dudu
