#include "dudu/lsp/language_server_member_references.hpp"

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"

#include <filesystem>

namespace dudu {
namespace {

bool position_contains_name(const Json* params, const std::string& name,
                            const SourceLocation& location) {
    const LspPosition position = lsp_position(params);
    const int target_line = position.line + 1;
    const int target_column = position.character + 1;
    if (name.empty() || location.line != target_line || location.column <= 0) {
        return false;
    }
    const int start = location.column;
    const int end = start + static_cast<int>(name.size());
    return target_column >= start && target_column <= end;
}

bool location_is_current_document(const SourceLocation& location, const Document& doc) {
    return location.file.empty() || std::filesystem::path(location.file) == doc.path;
}

std::optional<std::string> field_query_at(const Document& doc, const Json* params,
                                          const std::string& owner, const FieldDecl& field) {
    if (location_is_current_document(field.location, doc) &&
        position_contains_name(params, field.name, field.location)) {
        return owner + "." + field.name;
    }
    return std::nullopt;
}

std::optional<std::string> constant_query_at(const Document& doc, const Json* params,
                                             const std::string& owner, const ConstDecl& constant) {
    if (location_is_current_document(constant.location, doc) &&
        position_contains_name(params, constant.name, constant.location)) {
        return owner + "." + constant.name;
    }
    return std::nullopt;
}

std::optional<std::string> method_query_at(const Document& doc, const Json* params,
                                           const std::string& owner, const FunctionDecl& method) {
    if (location_is_current_document(method.location, doc) &&
        position_contains_name(params, method.name, method.location)) {
        return owner + "." + method.name;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> member_declaration_reference_query_at(const Document& doc,
                                                                 const Json* params,
                                                                 const ModuleAst* module) {
    if (module == nullptr) {
        return std::nullopt;
    }
    for (const ClassDecl& klass : module->classes) {
        if (!location_is_current_document(klass.location, doc)) {
            continue;
        }
        for (const FieldDecl& field : klass.fields) {
            if (const std::optional<std::string> query =
                    field_query_at(doc, params, klass.name, field)) {
                return query;
            }
        }
        for (const ConstDecl& constant : klass.constants) {
            if (const std::optional<std::string> query =
                    constant_query_at(doc, params, klass.name, constant)) {
                return query;
            }
        }
        for (const ConstDecl& field : klass.static_fields) {
            if (const std::optional<std::string> query =
                    constant_query_at(doc, params, klass.name, field)) {
                return query;
            }
        }
        for (const FunctionDecl& method : klass.methods) {
            if (const std::optional<std::string> query =
                    method_query_at(doc, params, klass.name, method)) {
                return query;
            }
        }
    }
    return std::nullopt;
}

} // namespace dudu
