#include "dudu/lsp/language_server_generic_params.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/ast_visit.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <set>

namespace dudu {
namespace {

bool contains_line(const SourceRange& range, int line) {
    const int end = range.end.line > 0 ? range.end.line : range.start.line;
    return range.start.line <= line && line <= end;
}

bool same_location(const SourceLocation& left, const SourceLocation& right) {
    return left.line == right.line && left.column == right.column &&
           left.file.str() == right.file.str();
}

const GenericParamDecl* find_param(const std::vector<GenericParamDecl>& params,
                                   const std::string& name) {
    for (const GenericParamDecl& param : params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

GenericParamTarget function_target(const FunctionDecl& fn, const GenericParamDecl& param,
                                   GenericParamOwnerKind kind) {
    const std::set<std::string> values = generic_value_params_for_function(fn);
    return {.name = param.name,
            .declaration = param.location,
            .owner = fn.location,
            .owner_kind = kind,
            .value = values.contains(param.name)};
}

GenericParamTarget class_target(const ClassDecl& klass, const GenericParamDecl& param) {
    const std::set<std::string> values = generic_value_params_for_class(klass);
    return {.name = param.name,
            .declaration = param.location,
            .owner = klass.location,
            .owner_kind = GenericParamOwnerKind::Class,
            .value = values.contains(param.name)};
}

struct GenericReferenceCollector {
    const Document& doc;
    const GenericParamTarget& target;
    std::vector<ReferenceLocation> out;
    std::set<std::pair<int, int>> seen;

    void add(SourceLocation location) {
        if (location.line <= 0 || location.column <= 0 ||
            !seen.insert({location.line, location.column}).second) {
            return;
        }
        SourceRange source_range{.start = location, .end = location};
        source_range.end.column += static_cast<int>(target.name.size());
        out.push_back(
            {.uri = uri_for_location(location, doc),
             .range = range_json(location.line - 1, location.column - 1,
                                 location.column - 1 + static_cast<int>(target.name.size())),
             .source_range = source_range});
    }

    void type(const TypeRef& type_ref) {
        visit_type_ref_tree(type_ref, [&](const TypeRef& child) {
            if (type_ref_head_name(child) == target.name) {
                add(child.location);
            }
        });
    }

    void expression(const Expr& expr) {
        visit_lsp_expr_tree(
            expr,
            [&](const Expr& child) {
                if (target.value && child.kind == ExprKind::Name && child.name == target.name) {
                    add(expr_name_location(child));
                }
            },
            [&](const TypeRef& child) { type(child); });
    }

    void statements(const std::vector<Stmt>& statements) {
        visit_lsp_stmt_tree(statements, [&](const Stmt& stmt) {
            type(stmt_type_ref(stmt));
            visit_stmt_expressions(stmt, [&](const Expr& expr) { expression(expr); });
        });
    }

    void function(const FunctionDecl& fn) {
        type(fn.receiver_type_ref);
        type(fn.return_type_ref);
        for (const ParamDecl& param : fn.params) {
            type(param.type_ref);
        }
        statements(fn.statements);
    }
};

} // namespace

std::optional<GenericParamTarget>
generic_param_target_at(const ModuleAst& module, LspPosition position, const std::string& name) {
    if (name.empty()) {
        return std::nullopt;
    }
    const int line = position.line + 1;
    for (const FunctionDecl& fn : module.functions) {
        if (!function_contains_source_line(fn, line)) {
            continue;
        }
        if (const GenericParamDecl* param = find_param(fn.generic_param_decls, name)) {
            return function_target(fn, *param, GenericParamOwnerKind::Function);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        if (!contains_line(klass.range, line)) {
            continue;
        }
        for (const FunctionDecl& method : klass.methods) {
            if (!function_contains_source_line(method, line)) {
                continue;
            }
            if (const GenericParamDecl* param = find_param(method.generic_param_decls, name)) {
                return function_target(method, *param, GenericParamOwnerKind::Method);
            }
            break;
        }
        if (const GenericParamDecl* param = find_param(klass.generic_param_decls, name)) {
            return class_target(klass, *param);
        }
    }
    return std::nullopt;
}

std::vector<ReferenceLocation> generic_param_references(const ModuleAst& module,
                                                        const Document& doc,
                                                        const GenericParamTarget& target) {
    GenericReferenceCollector collector{.doc = doc, .target = target, .out = {}, .seen = {}};
    collector.add(target.declaration);
    if (target.owner_kind == GenericParamOwnerKind::Function) {
        for (const FunctionDecl& fn : module.functions) {
            if (same_location(fn.location, target.owner)) {
                collector.function(fn);
                break;
            }
        }
        return std::move(collector.out);
    }
    for (const ClassDecl& klass : module.classes) {
        if (target.owner_kind == GenericParamOwnerKind::Class &&
            same_location(klass.location, target.owner)) {
            for (const BaseClassDecl& base : klass.base_class_refs) {
                collector.type(base.type_ref);
            }
            for (const FieldDecl& field : klass.fields) {
                collector.type(field.type_ref);
                collector.expression(field.value_expr);
            }
            for (const ConstDecl& constant : klass.constants) {
                collector.type(constant.type_ref);
                collector.expression(constant.value_expr);
            }
            for (const ConstDecl& field : klass.static_fields) {
                collector.type(field.type_ref);
                collector.expression(field.value_expr);
            }
            for (const FunctionDecl& method : klass.methods) {
                if (find_param(method.generic_param_decls, target.name) == nullptr) {
                    collector.function(method);
                }
            }
            break;
        }
        if (target.owner_kind != GenericParamOwnerKind::Method) {
            continue;
        }
        for (const FunctionDecl& method : klass.methods) {
            if (same_location(method.location, target.owner)) {
                collector.function(method);
                return std::move(collector.out);
            }
        }
    }
    return std::move(collector.out);
}

} // namespace dudu
