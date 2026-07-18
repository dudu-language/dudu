#include "dudu/lsp/language_server_scope.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/match_patterns.hpp"
#include "dudu/sema/sema_body_helpers.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_enum.hpp"
#include "dudu/sema/sema_expr.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <exception>

namespace dudu {

void bind_lsp_local(FunctionScope& scope, const std::string& name, TypeRef type) {
    if (name.empty()) {
        return;
    }
    if (!has_type_ref(type)) {
        type = named_type_ref("auto");
    }
    bind_local(scope, name, type);
}

TypeRef infer_lsp_expr_type(FunctionScope& scope, const Expr& expr) {
    return infer_expr_type_ast(scope, expr, &diagnostic_location(expr.location, expr));
}

TypeRef try_infer_lsp_expr_type(FunctionScope& scope, const Expr& expr) {
    try {
        return infer_lsp_expr_type(scope, expr);
    } catch (const std::exception&) {
        return {};
    }
}

TypeRef lsp_variable_type(const Stmt& stmt) {
    if (!has_stmt_type_ref(stmt)) {
        return {};
    }
    const TypeRef& declared = stmt_type_ref(stmt);
    const ArrayShapeInference inferred = infer_array_literal_shape_type(declared, stmt.value_expr);
    return inferred.status == ArrayShapeStatus::Inferred ? inferred.type_ref : declared;
}

std::optional<TypeRef> infer_lsp_loop_binding_type(FunctionScope& scope, const Stmt& stmt) {
    try {
        return infer_for_binding_type(scope, stmt);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void bind_lsp_tuple_names(FunctionScope& scope, const Stmt& stmt) {
    const std::vector<std::string> names = tuple_binding_names(stmt_target_expr(stmt));
    if (names.empty()) {
        return;
    }
    const std::vector<TypeRef> types = template_type_arg_refs_with_aliases(
        infer_lsp_expr_type(scope, stmt.value_expr), "tuple", scope.symbols.alias_type_refs);
    if (names.size() != types.size()) {
        return;
    }
    for (size_t i = 0; i < names.size(); ++i) {
        bind_lsp_local(scope, names[i], types[i]);
    }
}

bool try_bind_lsp_tuple_names(FunctionScope& scope, const Stmt& stmt) {
    try {
        bind_lsp_tuple_names(scope, stmt);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void bind_lsp_statement(FunctionScope& scope, const Stmt& stmt) {
    if (stmt.kind == StmtKind::VarDecl) {
        const TypeRef type = has_stmt_type_ref(stmt) ? lsp_variable_type(stmt)
                                                     : infer_lsp_expr_type(scope, stmt.value_expr);
        bind_lsp_local(scope, stmt.name, type);
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (!tuple_binding_names(stmt_target_expr(stmt)).empty()) {
            bind_lsp_tuple_names(scope, stmt);
            return;
        }
        const Expr& target = stmt_target_expr(stmt);
        if (target.kind == ExprKind::Name && !scope.local_type_refs.contains(target.name.str())) {
            bind_lsp_local(scope, target.name.str(), infer_lsp_expr_type(scope, stmt.value_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::Except && !stmt.name.empty()) {
        bind_lsp_local(scope, stmt.name, stmt_type_ref(stmt));
    }
}

void bind_lsp_match_case(FunctionScope& scope, const TypeRef& subject_type,
                         const Stmt& case_statement) {
    const WrapperMatchType wrapper = wrapper_match_type(subject_type);
    if (wrapper.kind != WrapperMatchKind::None) {
        const std::optional<std::string> binding =
            wrapper_case_binding_name(stmt_pattern_expr(case_statement));
        const std::optional<std::string> variant =
            wrapper_case_name(stmt_pattern_expr(case_statement));
        if (!binding || !variant) {
            return;
        }
        if (wrapper.kind == WrapperMatchKind::Option && *variant == "Some" &&
            wrapper.arg_refs.size() == 1) {
            bind_lsp_local(scope, *binding, wrapper.arg_refs[0]);
        } else if (wrapper.kind == WrapperMatchKind::Result && wrapper.arg_refs.size() == 2) {
            if (*variant == "Ok") {
                bind_lsp_local(scope, *binding, wrapper.arg_refs[0]);
            } else if (*variant == "Err") {
                bind_lsp_local(scope, *binding, wrapper.arg_refs[1]);
            }
        }
        return;
    }

    const EnumDecl* en = enum_decl_for_type(scope.symbols, subject_type);
    if (en == nullptr) {
        return;
    }
    const std::optional<std::string> variant = enum_case_variant_name_for(*en, case_statement);
    if (!variant || *variant == "_") {
        return;
    }
    const EnumValueDecl* value = enum_variant_decl(*en, *variant);
    if (value == nullptr) {
        return;
    }
    for (const EnumCaseBinding& binding : enum_case_bindings(case_statement, *value)) {
        if (binding.field_index < value->payload_fields.size()) {
            bind_lsp_local(scope, binding.name,
                           value->payload_fields[binding.field_index].type_ref);
        }
    }
}

Symbols symbols_for_lsp_function(Symbols symbols, const FunctionDecl& fn) {
    if (!fn.generic_params.empty()) {
        symbols = with_generic_params(std::move(symbols), fn.generic_params,
                                      generic_value_params_for_function(fn));
    }
    return symbols;
}

void bind_lsp_function_params(FunctionScope& scope, const FunctionDecl& fn) {
    for (const ParamDecl& param : fn.params) {
        TypeRef type = param.type_ref;
        if (param.name == "self" && !scope.current_class.empty()) {
            if (!has_type_ref(type)) {
                type = named_type_ref(scope.current_class, param.location);
            } else {
                type = substitute_type_ref(
                    type, {{"Self", named_type_ref(scope.current_class, param.location)}});
            }
        }
        bind_lsp_local(scope, param.name, std::move(type));
    }
}

} // namespace dudu
