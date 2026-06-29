#include "dudu/lsp/language_server_member_references.hpp"

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_navigation.hpp"

#include <filesystem>
#include <set>

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

bool class_has_member_named(const ClassDecl& klass, const std::string& member) {
    for (const FieldDecl& field : klass.fields) {
        if (field.name == member) {
            return true;
        }
    }
    for (const ConstDecl& constant : klass.constants) {
        if (constant.name == member) {
            return true;
        }
    }
    for (const ConstDecl& field : klass.static_fields) {
        if (field.name == member) {
            return true;
        }
    }
    for (const FunctionDecl& method : klass.methods) {
        if (method.name == member) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> unique_member_owner(const ModuleAst& module,
                                               const std::set<std::string>& candidate_types,
                                               const std::string& member) {
    std::optional<std::string> owner;
    const auto consider = [&](const ClassDecl& klass) {
        if (!candidate_types.contains(klass.name) || !class_has_member_named(klass, member)) {
            return;
        }
        if (owner.has_value() && *owner != klass.name) {
            owner = "";
            return;
        }
        owner = klass.name;
    };
    for (const ClassDecl& klass : module.classes) {
        consider(klass);
    }
    for (const ClassDecl& klass : module.native_classes) {
        consider(klass);
    }
    if (!owner.has_value() && candidate_types.size() == 1) {
        return *candidate_types.begin();
    }
    if (!owner.has_value() || owner->empty()) {
        return std::nullopt;
    }
    return owner;
}

} // namespace

std::optional<std::string> enum_value_declaration_reference_query_at(const Document& doc,
                                                                     const Json* params,
                                                                     const ModuleAst* module) {
    if (module == nullptr) {
        return std::nullopt;
    }
    for (const EnumDecl& en : module->enums) {
        if (!location_is_current_document(en.location, doc)) {
            continue;
        }
        for (const EnumValueDecl& value : en.values) {
            if (location_is_current_document(value.location, doc) &&
                position_contains_name(params, value.name, value.location)) {
                return en.name + "." + value.name;
            }
        }
    }
    return std::nullopt;
}

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

std::optional<std::string> member_use_reference_query_at(const ModuleAst& module,
                                                         const ExprPath& path, const Json* params) {
    if (path.segments.size() != 2 || path.segments[0].kind != ExprPathSegmentKind::Name ||
        path.segments[1].kind != ExprPathSegmentKind::Name) {
        return std::nullopt;
    }
    const TypeRef receiver_type =
        local_type_ref_before_cursor(module, path.segments[0].text, params);
    if (!has_type_ref(receiver_type)) {
        return std::nullopt;
    }
    const std::set<std::string> candidate_types = member_candidate_types(module, receiver_type);
    if (candidate_types.empty()) {
        const std::string owner = type_ref_head_name(receiver_type);
        if (!owner.empty()) {
            return owner + "." + path.segments[1].text;
        }
        return std::nullopt;
    }
    const std::optional<std::string> owner =
        unique_member_owner(module, candidate_types, path.segments[1].text);
    if (!owner.has_value()) {
        const std::string head = type_ref_head_name(receiver_type);
        if (!head.empty() && candidate_types.contains(head)) {
            return head + "." + path.segments[1].text;
        }
        return std::nullopt;
    }
    return *owner + "." + path.segments[1].text;
}

} // namespace dudu
