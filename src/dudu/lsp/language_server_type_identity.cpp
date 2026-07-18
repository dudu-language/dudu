#include "dudu/lsp/language_server_type_identity.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_method_templates.hpp"

#include <algorithm>

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

std::optional<Symbol> native_symbol_for_identity(const std::vector<Symbol>& symbols,
                                                 std::string_view identity) {
    const auto matches_identity = [&](const Symbol& symbol) {
        return symbol.native_identity_key.has_value() && *symbol.native_identity_key == identity;
    };
    const auto selected = std::find_if(symbols.begin(), symbols.end(), [&](const Symbol& symbol) {
        return matches_identity(symbol) && symbol.kind == lsp_symbol_kind::Class;
    });
    if (selected != symbols.end()) {
        return *selected;
    }
    const auto fallback = std::find_if(symbols.begin(), symbols.end(), matches_identity);
    return fallback == symbols.end() ? std::nullopt : std::optional<Symbol>{*fallback};
}

const ClassDecl* native_class_for_identity(const ModuleAst& module, std::string_view identity) {
    for (const ClassDecl& klass : module.native_classes) {
        if (native_identity_key(klass.identity) == identity) {
            return &klass;
        }
    }
    return nullptr;
}

std::optional<Symbol> native_type_symbol_for_type_ref(const ModuleAst& module,
                                                      const TypeRef& type) {
    const std::string binding = type_ref_binding_name(type);
    if (binding.empty()) {
        return std::nullopt;
    }
    const bool native_binding =
        std::ranges::any_of(module.native_types,
                            [&](const NativeTypeDecl& decl) { return decl.name == binding; }) ||
        std::ranges::any_of(module.native_classes,
                            [&](const ClassDecl& decl) { return decl.name == binding; });
    if (!native_binding) {
        return std::nullopt;
    }
    const Symbols sema_symbols = collect_symbols(module);
    const auto identity = sema_symbols.native_type_identity_by_binding.find(binding);
    if (identity == sema_symbols.native_type_identity_by_binding.end()) {
        return std::nullopt;
    }
    std::optional<Symbol> selected =
        native_symbol_for_identity(symbols_for_module(module, true), identity->second);
    if (!selected.has_value()) {
        return std::nullopt;
    }

    std::string detail = "type " + type_ref_text(type);
    try {
        const TypeRef resolved = resolve_associated_type_ref(sema_symbols, type);
        const std::string resolved_text = type_ref_text(resolved);
        if (!resolved_text.empty() && resolved_text != type_ref_text(type)) {
            detail += " = " + resolved_text;
        }
    } catch (const CompileError&) {
    }
    selected->name = binding;
    selected->detail = std::move(detail);
    return selected;
}

} // namespace dudu
