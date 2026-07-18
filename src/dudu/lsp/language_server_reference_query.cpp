#include "dudu/lsp/language_server_reference_query.hpp"

#include "dudu/lsp/language_server_import_references.hpp"
#include "dudu/lsp/language_server_member_references.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_operator.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/lsp/language_server_type_identity.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {
namespace {

bool document_has_type_symbol(const std::vector<Symbol>& symbols, const std::string& name) {
    for (const Symbol& symbol : symbols) {
        if (symbol.name == name &&
            (symbol.kind == lsp_symbol_kind::Class || symbol.kind == lsp_symbol_kind::Struct)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string reference_query_at(const Document& doc, const Json* params,
                               const AstSelection& selection, const ModuleAst* module,
                               const std::vector<Symbol>& symbols_with_native) {
    if (const std::optional<std::string> member_query =
            member_declaration_reference_query_at(doc, params, module)) {
        return *member_query;
    }
    if (const std::optional<std::string> enum_value_query =
            enum_value_declaration_reference_query_at(doc, params, module)) {
        return *enum_value_query;
    }
    if (module != nullptr && selection.operator_expr) {
        if (const std::optional<Symbol> op = dudu_operator_symbol_for_expr(
                *module, *selection.operator_expr, lsp_position(params).line + 1)) {
            return op->name;
        }
    }
    if (selection.type_ref) {
        const std::string binding = type_ref_binding_name(*selection.type_ref);
        if (document_has_type_symbol(symbols_with_native, binding)) {
            return binding;
        }
    }
    const std::string name = selection.symbol.value_or("");
    std::optional<std::string> expression_path;
    std::vector<std::string> paths;
    if (selection.symbol_path.has_value()) {
        paths.push_back(*selection.symbol_path);
    }
    if (selection.expr_path.has_value()) {
        if (const std::optional<std::string> member_query =
                module == nullptr
                    ? std::nullopt
                    : member_use_reference_query_at(*module, doc, *selection.expr_path, params)) {
            return *member_query;
        }
        expression_path = render_expr_path(*selection.expr_path);
        paths.push_back(*expression_path);
    }
    const std::string selected_path =
        selection.symbol_path.value_or(expression_path.value_or(name));
    if (const std::optional<Symbol> native_namespace =
            native_namespace_segment_symbol(symbols_with_native, selection.symbol, selected_path)) {
        return native_namespace->name;
    }
    if (module != nullptr) {
        for (const std::string& path : paths) {
            if (path.empty() || path == name || path.find('.') == std::string::npos) {
                continue;
            }
            if (native_alias_target_class_definition(*module, path).has_value()) {
                return path;
            }
            if (module_import_target_key(doc, path).has_value()) {
                return path;
            }
            for (const ClassDecl& klass : module->native_classes) {
                if (klass.name == path) {
                    return path;
                }
            }
            for (const NativeFunctionDecl& fn : module->native_functions) {
                if (fn.name == path) {
                    return path;
                }
            }
            for (const NativeValueDecl& value : module->native_values) {
                if (value.name == path) {
                    return path;
                }
            }
            for (const NativeMacroDecl& macro : module->native_macros) {
                if (macro.name == path) {
                    return path;
                }
            }
            if (document_has_type_symbol(symbols_with_native, path)) {
                return path;
            }
        }
    }
    if (expression_path.has_value() && expression_path->find('.') != std::string::npos) {
        return *expression_path;
    }
    return name;
}

} // namespace dudu
