#include "dudu/lsp/language_server_import_references.hpp"

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_support.hpp"

#include <exception>

namespace dudu {
namespace {

std::string dotted_head(const std::string& query) {
    const size_t dot = query.find('.');
    return dot == std::string::npos ? query : query.substr(0, dot);
}

} // namespace

bool same_import_reference_target(const std::optional<ImportReferenceTarget>& lhs,
                                  const std::optional<ImportReferenceTarget>& rhs) {
    if (!lhs.has_value() || !rhs.has_value()) {
        return lhs.has_value() == rhs.has_value();
    }
    return lhs->source_key == rhs->source_key && lhs->member_name == rhs->member_name;
}

std::optional<ImportReferenceTarget> selective_import_target(const Document& doc,
                                                             const std::string& query) {
    if (query.empty() || query.find('.') != std::string::npos) {
        return std::nullopt;
    }
    try {
        const ProjectIndex& index = project_index_for_document(doc, false);
        const ModuleAst& current = index.visible_unit_for_path(doc.path);
        for (const ImportDecl& import : current.imports) {
            if (import.kind != ImportKind::From || bound_import_name(import) != query) {
                continue;
            }
            if (const ModuleAst* imported = index.imported_unit(current, import)) {
                return ImportReferenceTarget{
                    .source_key = imported->source_path.empty() ? imported->module_path
                                                               : imported->source_path.string(),
                    .member_name = import.imported_name};
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> module_import_target_key(const Document& doc,
                                                    const std::string& dotted_query) {
    if (dotted_query.find('.') == std::string::npos) {
        return std::nullopt;
    }
    const std::string head = dotted_head(dotted_query);
    try {
        const ProjectIndex& index = project_index_for_document(doc, false);
        const ModuleAst& current = index.visible_unit_for_path(doc.path);
        for (const ImportDecl& import : current.imports) {
            if (import.kind != ImportKind::Module || bound_import_name(import) != head) {
                continue;
            }
            if (const ModuleAst* imported = index.imported_unit(current, import)) {
                return imported->source_path.empty() ? imported->module_path
                                                     : imported->source_path.string();
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace dudu
