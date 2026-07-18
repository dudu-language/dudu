#include "dudu/lsp/language_server_type_identity.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_method_templates.hpp"

namespace dudu {

std::string type_ref_binding_name(const TypeRef& type) {
    if ((type.kind == TypeKind::Associated || type.kind == TypeKind::AssociatedTemplate) &&
        !type.children.empty()) {
        const std::string owner = type_ref_binding_name(type.children.front());
        return owner.empty() ? std::string{} : owner + "." + type.name;
    }
    if (type.kind == TypeKind::Shaped && !type.children.empty()) {
        return type_ref_binding_name(type.children.front());
    }
    return type_ref_head_name(type);
}

std::optional<Symbol> native_type_symbol_for_type_ref(const ModuleAst& module,
                                                      const TypeRef& type) {
    if (type.kind != TypeKind::Associated && type.kind != TypeKind::AssociatedTemplate) {
        return std::nullopt;
    }
    const std::string binding = type_ref_binding_name(type);
    if (binding.empty()) {
        return std::nullopt;
    }
    const NativeTypeDecl* selected = nullptr;
    for (const NativeTypeDecl& candidate : module.native_types) {
        if (candidate.name != binding) {
            continue;
        }
        if (selected != nullptr && native_identity_key(selected->identity) !=
                                       native_identity_key(candidate.identity)) {
            return std::nullopt;
        }
        selected = &candidate;
    }
    if (selected == nullptr) {
        return std::nullopt;
    }

    std::string detail = "type " + type_ref_text(type);
    try {
        const TypeRef resolved = resolve_associated_type_ref(collect_symbols(module), type);
        const std::string resolved_text = type_ref_text(resolved);
        if (!resolved_text.empty() && resolved_text != type_ref_text(type)) {
            detail += " = " + resolved_text;
        }
    } catch (const CompileError&) {
    }
    return Symbol{.name = binding,
                  .detail = std::move(detail),
                  .location = selected->location,
                  .kind = lsp_symbol_kind::Struct,
                  .native_identity_key = native_identity_key(selected->identity),
                  .doc_comment = selected->doc_comment};
}

} // namespace dudu
