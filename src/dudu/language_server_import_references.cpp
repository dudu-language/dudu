#include "dudu/language_server_import_references.hpp"

#include "dudu/ast.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/module_names.hpp"

#include <exception>
#include <filesystem>
#include <system_error>

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
        const ModuleAst module = module_for_document(doc, false);
        const ModuleAst& current = visible_module_unit(module, doc.path);
        for (const ImportDecl& import : current.imports) {
            if (import.kind != ImportKind::From || bound_import_name(import) != query) {
                continue;
            }
            if (const ModuleAst* imported = imported_module_unit(module, current, import)) {
                return ImportReferenceTarget{
                    .source_key = imported->source_path.empty() ? imported->module_path
                                                               : imported->source_path.string(),
                    .member_name = import.imported_name};
            }
            std::error_code error;
            const std::filesystem::path base =
                current.source_path.empty() ? doc.path : current.source_path;
            const std::filesystem::path resolved = std::filesystem::weakly_canonical(
                module_path_to_file(base.parent_path(), import.module_path), error);
            if (!error) {
                return ImportReferenceTarget{.source_key = resolved.string(),
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
        const ModuleAst module = module_for_document(doc, false);
        const ModuleAst& current = visible_module_unit(module, doc.path);
        for (const ImportDecl& import : current.imports) {
            if (import.kind != ImportKind::Module || bound_import_name(import) != head) {
                continue;
            }
            if (const ModuleAst* imported = imported_module_unit(module, current, import)) {
                return imported->source_path.empty() ? imported->module_path
                                                     : imported->source_path.string();
            }
            std::error_code error;
            const std::filesystem::path base =
                current.source_path.empty() ? doc.path : current.source_path;
            const std::filesystem::path resolved = std::filesystem::weakly_canonical(
                module_path_to_file(base.parent_path(), import.module_path), error);
            if (!error) {
                return resolved.string();
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace dudu
