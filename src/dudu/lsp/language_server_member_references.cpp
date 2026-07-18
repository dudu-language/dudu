#include "dudu/lsp/language_server_member_references.hpp"

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/ast_visit.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_member_identity.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_operator.hpp"
#include "dudu/sema/sema_context.hpp"

#include <filesystem>
#include <map>
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

std::optional<MemberReferenceTarget>
member_declaration_target_at(const Document& doc, const Json* params, const ModuleAst& module) {
    for (const ClassDecl& klass : module.classes) {
        if (!location_is_current_document(klass.location, doc)) {
            continue;
        }
        const auto target =
            [&](const std::string& name,
                const SourceLocation& location) -> std::optional<MemberReferenceTarget> {
            if (!location_is_current_document(location, doc) ||
                !position_contains_name(params, name, location)) {
                return std::nullopt;
            }
            return MemberReferenceTarget{.name = name, .declaration = location};
        };
        for (const FieldDecl& field : klass.fields) {
            if (const auto result = target(field.name, field.location))
                return result;
        }
        for (const ConstDecl& constant : klass.constants) {
            if (const auto result = target(constant.name, constant.location))
                return result;
        }
        for (const ConstDecl& field : klass.static_fields) {
            if (const auto result = target(field.name, field.location))
                return result;
        }
        for (const FunctionDecl& method : klass.methods) {
            if (const auto result = target(method.name, method.location))
                return result;
        }
    }
    return std::nullopt;
}

ReferenceLocation reference_location(const Document& doc, const std::string& name,
                                     const SourceLocation& location) {
    const int line = location.line - 1;
    const int start = location.column - 1;
    SourceRange source_range{.start = location, .end = location};
    source_range.end.column += static_cast<int>(name.size());
    return {.uri = uri_for_location(location, doc),
            .range = range_json(line, start, start + static_cast<int>(name.size())),
            .source_range = std::move(source_range)};
}

} // namespace

std::optional<MemberReferenceTarget> member_reference_target_at(const Document& doc,
                                                                const Json* params,
                                                                const AstSelection& selection,
                                                                const ModuleAst* module) {
    if (module == nullptr) {
        return std::nullopt;
    }
    if (const auto declaration = member_declaration_target_at(doc, params, *module)) {
        return declaration;
    }
    if (!selection.member_expr.has_value()) {
        return std::nullopt;
    }
    const SourceLocation location{
        .file = SourceFileName(doc.path.string()),
        .line = lsp_position(params).line + 1,
        .column = lsp_position(params).character + 1,
    };
    const Expr* call =
        selection.call_callee && selection.call_expr ? &*selection.call_expr : nullptr;
    const std::optional<Symbol> symbol =
        dudu_member_symbol_for_expr(*module, *selection.member_expr, location, call);
    if (!symbol.has_value()) {
        return std::nullopt;
    }
    return MemberReferenceTarget{.name = symbol->name, .declaration = symbol->location};
}

std::vector<ReferenceLocation> member_reference_locations(const ModuleAst& module,
                                                          const Document& doc,
                                                          const MemberReferenceTarget& target) {
    std::vector<ReferenceLocation> out;
    std::set<std::pair<int, int>> seen;
    const auto add = [&](const std::string& name, const SourceLocation& location) {
        if (!same_member_declaration(location, target.declaration) || location.line <= 0 ||
            location.column <= 0 || !seen.insert({location.line, location.column}).second) {
            return;
        }
        out.push_back(reference_location(doc, name, location));
    };
    for (const ClassDecl& klass : module.classes) {
        for (const FieldDecl& field : klass.fields)
            add(field.name, field.location);
        for (const ConstDecl& constant : klass.constants)
            add(constant.name, constant.location);
        for (const ConstDecl& field : klass.static_fields)
            add(field.name, field.location);
        for (const FunctionDecl& method : klass.methods)
            add(method.name, method.location);
    }
    const auto visit_statements = [&](const std::vector<Stmt>& statements) {
        visit_lsp_stmt_tree(statements, [&](const Stmt& stmt) {
            visit_stmt_expressions(stmt, [&](const Expr& root) {
                visit_lsp_expr_tree(
                    root,
                    [&](const Expr& expr) {
                        if ((expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
                            has_expr_callee(expr) &&
                            expr_callee(expr).front().kind == ExprKind::Member) {
                            const Expr& member = expr_callee(expr).front();
                            const SourceLocation use_location = expr_name_location(member);
                            const std::optional<Symbol> symbol =
                                dudu_member_symbol_for_expr(module, member, use_location, &expr);
                            if (symbol && member.name == target.name &&
                                same_member_declaration(symbol->location, target.declaration) &&
                                seen.insert({use_location.line, use_location.column}).second) {
                                out.push_back(reference_location(doc, member.name, use_location));
                            }
                            return;
                        }
                        if (expr.kind == ExprKind::Binary) {
                            const std::optional<Symbol> symbol =
                                dudu_operator_symbol_for_expr(module, expr, expr.location.line);
                            if (symbol.has_value() &&
                                same_member_declaration(symbol->location, target.declaration) &&
                                seen.insert({expr.op_location.line, expr.op_location.column})
                                    .second) {
                                out.push_back(reference_location(doc, std::string(expr.op),
                                                                 expr.op_location));
                            }
                            return;
                        }
                        if (expr.kind != ExprKind::Member)
                            return;
                        if (expr.name != target.name) {
                            return;
                        }
                        const SourceLocation use_location = expr_name_location(expr);
                        const std::optional<Symbol> symbol =
                            dudu_member_symbol_for_expr(module, expr, use_location);
                        if (symbol.has_value() &&
                            same_member_declaration(symbol->location, target.declaration) &&
                            seen.insert({use_location.line, use_location.column}).second) {
                            out.push_back(reference_location(doc, expr.name, use_location));
                        }
                    },
                    [](const TypeRef&) {});
            });
        });
    };
    for (const FunctionDecl& function : module.functions)
        visit_statements(function.statements);
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods)
            visit_statements(method.statements);
    }
    return out;
}

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
                                                         const Document& doc, const ExprPath& path,
                                                         const Json* params) {
    if (path.segments.size() != 2 || path.segments[0].kind != ExprPathSegmentKind::Name ||
        path.segments[1].kind != ExprPathSegmentKind::Name) {
        return std::nullopt;
    }
    const TypeRef receiver_type =
        local_type_ref_before_cursor(module, doc, path.segments[0].text, params);
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
