#include "dudu/language_server_native_lookup.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/native_header_identity.hpp"

#include <map>
#include <utility>

namespace dudu {
namespace {

struct NativeClassDefinitionIndex {
    std::map<std::string, NativeClassDefinition> by_name;
    std::map<std::string, NativeClassDefinition> by_identity;
};

NativeClassDefinitionIndex native_class_definition_index(const ModuleAst& module) {
    NativeClassDefinitionIndex out;
    for (const ClassDecl& klass : module.native_classes) {
        NativeClassDefinition definition{.name = klass.name, .location = klass.location};
        if (!klass.name.empty()) {
            out.by_name.emplace(klass.name, definition);
        }
        const std::string identity = native_symbol_identity_key(klass.identity);
        if (!identity.empty()) {
            out.by_identity.emplace(identity, std::move(definition));
        }
    }
    return out;
}

} // namespace

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const ModuleAst& module, const NativeTypeDecl& alias) {
    if (!has_type_ref(alias.type_ref)) {
        return std::nullopt;
    }

    const NativeClassDefinitionIndex class_index = native_class_definition_index(module);
    const std::string identity = native_symbol_identity_key(alias.identity);
    if (!identity.empty()) {
        if (const auto found = class_index.by_identity.find(identity);
            found != class_index.by_identity.end()) {
            return found->second;
        }
    }

    const std::string target_name = type_ref_head_name(native_type_alias_type_ref(alias));
    if (target_name.empty() || target_name == alias.name) {
        return std::nullopt;
    }
    if (const auto found = class_index.by_name.find(target_name);
        found != class_index.by_name.end()) {
        return found->second;
    }
    return std::nullopt;
}

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const ModuleAst& module, const std::string& alias_name) {
    for (const NativeTypeDecl& type : module.native_types) {
        if (type.name == alias_name) {
            return native_alias_target_class_definition(module, type);
        }
    }
    return std::nullopt;
}

} // namespace dudu
