#include "dudu/lsp/language_server_native_lookup.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_header_identity.hpp"

#include <filesystem>
#include <utility>

namespace dudu {

NativeClassDefinitionIndex native_class_definition_index(const ModuleAst& module) {
    NativeClassDefinitionIndex out;
    for (const ClassDecl& klass : module.native_classes) {
        NativeClassDefinition definition{
            .name = klass.name, .location = klass.location, .doc_comment = klass.doc_comment};
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

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const NativeClassDefinitionIndex& class_index,
                                     const NativeTypeDecl& alias) {
    if (!has_type_ref(alias.type_ref)) {
        return std::nullopt;
    }

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
native_alias_target_class_definition(const ModuleAst& module, const NativeTypeDecl& alias) {
    return native_alias_target_class_definition(native_class_definition_index(module), alias);
}

std::optional<NativeClassDefinition>
native_alias_target_class_definition(const ModuleAst& module, const std::string& alias_name) {
    const NativeClassDefinitionIndex class_index = native_class_definition_index(module);
    for (const NativeTypeDecl& type : module.native_types) {
        if (type.name == alias_name) {
            return native_alias_target_class_definition(class_index, type);
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> native_identity_source_path(std::string_view identity) {
    if (!identity.starts_with("usr:")) {
        return std::nullopt;
    }
    const size_t delimiter = identity.find("::", 4);
    if (delimiter == std::string_view::npos) {
        return std::nullopt;
    }
    const std::filesystem::path path(identity.substr(4, delimiter - 4));
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

} // namespace dudu
