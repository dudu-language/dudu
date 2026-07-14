#include "dudu/sema/sema_body.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/control_flow.hpp"
#include "dudu/core/escapes.hpp"
#include "dudu/core/naming.hpp"
#include "dudu/core/source.hpp"
#include "dudu/sema/collection_literal_inference.hpp"
#include "dudu/sema/sema_alloc.hpp"
#include "dudu/sema/sema_assignment.hpp"
#include "dudu/sema/sema_bindings.hpp"
#include "dudu/sema/sema_body_helpers.hpp"
#include "dudu/sema/sema_body_internal.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_match.hpp"
#include "dudu/sema/sema_super.hpp"

#include <optional>

namespace dudu {
namespace {
void check_stmt(FunctionScope& scope, const Stmt& stmt, const TypeRef& return_type_ref,
                int loop_depth);

void reject_standalone_slice_value(const Expr& expr) {
    if (expr.kind == ExprKind::Slice) {
        sema_fail(expr.location, "slice expression must be used inside an index");
    }
    if (expr.kind == ExprKind::Ellipsis) {
        sema_fail(expr.location, "ellipsis must be used inside an index");
    }
    if (expr.kind == ExprKind::NewAxis) {
        sema_fail(expr.location, "None new-axis item must be used inside an index");
    }
}

TypeRef generic_self_type_ref(const ClassDecl& klass, SourceLocation location) {
    if (klass.generic_params.empty()) {
        return named_type_ref(klass.name, location);
    }
    TypeRef out = named_type_ref(klass.name, location);
    out.kind = TypeKind::Template;
    for (const std::string& param : klass.generic_params) {
        out.children.push_back(named_type_ref(param, location));
    }
    return out;
}

TypeRef substitute_method_self_type(const ClassDecl& klass, const TypeRef& type,
                                    SourceLocation location) {
    if (klass.generic_params.empty()) {
        return type;
    }
    return substitute_type_ref(type, {{"Self", generic_self_type_ref(klass, location)}});
}

TypeRef infer_binding_type(FunctionScope& scope, const Expr& expr, const SourceLocation& location) {
    if (!is_collection_literal(expr)) {
        return infer_expr_type_ast(scope, expr, &location);
    }
    const CollectionLiteralInference inferred =
        infer_collection_literal_type(&scope.symbols, expr, [&](const Expr& child) {
            const SourceLocation& child_location = diagnostic_location(location, child);
            return infer_expr_type_ast(scope, child, &child_location);
        });
    if (inferred.status != CollectionLiteralStatus::Inferred) {
        const SourceLocation error_location =
            inferred.error_location.line > 0 ? inferred.error_location : location;
        sema_fail(error_location, collection_literal_error(inferred));
    }
    return inferred.type_ref;
}

void check_block(FunctionScope& scope, const std::vector<Stmt>& body,
                 const TypeRef& return_type_ref, int loop_depth) {
    const bool allow_super_init_at_start = scope.allow_super_init;
    for (size_t i = 0; i < body.size(); ++i) {
        const Stmt& stmt = body[i];
        scope.allow_super_init =
            allow_super_init_at_start && i == 0 && loop_depth == 0 && is_super_init_stmt(stmt);
        check_stmt(scope, stmt, return_type_ref, loop_depth);
    }
    scope.allow_super_init = false;
}

void check_known_scoped_type_ref(const FunctionScope& scope, const SourceLocation& location,
                                 const TypeRef& type, const std::string& message) {
    if (const auto unknown = unknown_type_ref(scope.symbols, type)) {
        const SourceLocation error_location = unknown->second.line > 0 ? unknown->second : location;
        if (scope.local_type_refs.contains(unknown->first)) {
            sema_fail(error_location, "value used as a type: " + unknown->first);
        }
        sema_fail(error_location, message + unknown->first);
    }
}

void check_stmt(FunctionScope& scope, const Stmt& stmt, const TypeRef& return_type_ref,
                int loop_depth) {
    check_local_address_escape(stmt, scope.local_type_refs);
    if (stmt.kind == StmtKind::Return) {
        const SourceLocation& value_location = diagnostic_location(stmt.location, stmt.value_expr);
        if (missing_expr(stmt.value_expr)) {
            if (!type_ref_is_void(return_type_ref)) {
                const std::string return_type = type_ref_text(return_type_ref);
                sema_fail(value_location,
                          "return type mismatch: expected " + return_type + ", got void");
            }
            return;
        }
        if (!type_ref_is_void(return_type_ref)) {
            check_type_ref_match(scope, return_type_ref, stmt.value_expr, value_location,
                                 "return type mismatch");
            return;
        }
        const TypeRef got_ref = infer_expr_type_ast(scope, stmt.value_expr, &value_location);
        if (!type_ref_is_void(got_ref)) {
            sema_fail(value_location, "void function cannot return " + type_ref_text(got_ref));
        }
        return;
    }
    if (stmt.kind == StmtKind::Assert || stmt.kind == StmtKind::DebugAssert) {
        const bool debug = stmt.kind == StmtKind::DebugAssert;
        if (!debug && freestanding_like(scope)) {
            sema_fail(stmt.location,
                      "runtime assert is not available in " + scope.target_mode +
                          " target mode; use debug_assert or a target-specific assert handler");
        }
        check_condition_type(scope, stmt);
        if (has_stmt_message_expr(stmt)) {
            const Expr& message = stmt_message_expr(stmt);
            (void)infer_expr_type_ast(scope, message, &diagnostic_location(stmt.location, message));
        }
        return;
    }
    if (stmt.kind == StmtKind::Raise) {
        if (sema_has_expr(stmt.value_expr)) {
            (void)infer_expr_type_ast(scope, stmt.value_expr,
                                      &diagnostic_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::Match) {
        check_match_stmt(scope, stmt, return_type_ref, loop_depth, {.check_block = check_block});
        return;
    }
    if (stmt.kind == StmtKind::Case) {
        sema_fail(stmt.location, "case outside match");
    }
    if (stmt.kind == StmtKind::Delete) {
        std::vector<TypeRef> arg_types;
        auto infer_type = [&](const Expr& expr) {
            return infer_expr_type_ast(scope, expr, &diagnostic_location(stmt.location, expr));
        };
        if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
            for (const Expr& child : stmt.value_expr.children)
                arg_types.push_back(infer_type(child));
        } else {
            arg_types.push_back(infer_type(stmt.value_expr));
        }
        check_deallocation_args(stmt.location, "delete", arg_types);
        return;
    }
    if (stmt.kind == StmtKind::CppEscape || stmt.kind == StmtKind::Pass) {
        return;
    }
    if ((stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) && loop_depth == 0) {
        sema_fail(stmt.location, std::string(statement_kind_name(stmt.kind)) + " outside loop");
    }
    if (stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) {
        return;
    }
    if (stmt.kind == StmtKind::If || stmt.kind == StmtKind::Elif) {
        check_condition_type(scope, stmt);
        check_block(scope, stmt.children, return_type_ref, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Else || stmt.kind == StmtKind::Try) {
        check_block(scope, stmt.children, return_type_ref, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        FunctionScope nested = scope;
        if (has_stmt_condition_expr(stmt) || (stmt.name.empty() != !has_stmt_type_ref(stmt))) {
            sema_fail(stmt.location, "expected except binding as name: Type");
        }
        if (!stmt.name.empty()) {
            check_local_binding_name(stmt.location, stmt.name);
            const TypeRef& declared_type = stmt_type_ref(stmt);
            check_known_scoped_type_ref(scope, diagnostic_location(stmt.location, declared_type),
                                        declared_type, "unknown catch type: ");
            TypeRef catch_type = const_reference_type_ref(declared_type);
            bind_local(nested, stmt.name, catch_type);
        }
        check_block(nested, stmt.children, return_type_ref, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::While) {
        check_condition_type(scope, stmt);
        check_block(scope, stmt.children, return_type_ref, loop_depth + 1);
        return;
    }
    if (stmt.kind == StmtKind::For) {
        FunctionScope nested = scope;
        if (!stmt.name.empty() && has_stmt_iterable_expr(stmt)) {
            check_local_binding_name(stmt.location, stmt.name);
            TypeRef binding_type = stmt_type_ref(stmt);
            if (!has_type_ref(binding_type)) {
                const auto inferred = infer_for_binding_type(scope, stmt);
                if (!inferred) {
                    sema_fail(diagnostic_location(stmt.location, stmt_iterable_expr(stmt)),
                              "cannot infer loop binding type");
                }
                binding_type = *inferred;
            } else {
                const TypeRef& declared_type = stmt_type_ref(stmt);
                check_known_scoped_type_ref(scope,
                                            diagnostic_location(stmt.location, declared_type),
                                            declared_type, "unknown loop binding type: ");
                check_iterable_binding(scope.symbols, scope.local_type_refs,
                                       diagnostic_location(stmt.location, stmt_iterable_expr(stmt)),
                                       binding_type, stmt_iterable_expr(stmt));
            }
            bind_local(nested, stmt.name, binding_type);
        }
        check_block(nested, stmt.children, return_type_ref, loop_depth + 1);
        return;
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        const TypeRef target_type = compound_assignment_target_type_ref(scope, stmt);
        if (has_type_ref(target_type)) {
            const Expr value = compound_assignment_value_expr(stmt);
            check_type_ref_match(scope, target_type, value,
                                 diagnostic_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        check_local_binding_name(stmt.location, stmt.name);
        reject_standalone_slice_value(stmt.value_expr);
        const TypeRef& declared_type = stmt_type_ref(stmt);
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(declared_type, stmt.value_expr);
        if (inferred.status == ArrayShapeStatus::EmptyLiteral) {
            sema_fail(diagnostic_location(stmt.location, stmt.value_expr),
                      "array shape cannot be inferred from an empty literal");
        }
        if (inferred.status == ArrayShapeStatus::RaggedLiteral) {
            sema_fail(inferred.error_location.line > 0 ? inferred.error_location : stmt.location,
                      "ragged array literal");
        }
        const std::vector<size_t> explicit_shape = explicit_array_shape(declared_type);
        const TypeRef explicit_element = explicit_array_element_type_ref(declared_type);
        if (!explicit_shape.empty() && stmt.value_expr.kind == ExprKind::ListLiteral) {
            const ArrayShapeInference actual =
                infer_array_literal_shape_type(declared_type.children.front(), stmt.value_expr);
            if (actual.status == ArrayShapeStatus::RaggedLiteral) {
                sema_fail(actual.error_location.line > 0 ? actual.error_location : stmt.location,
                          "ragged array literal");
            }
            if (actual.status == ArrayShapeStatus::EmptyLiteral &&
                explicit_shape != std::vector<size_t>{0}) {
                sema_fail(diagnostic_location(stmt.location, stmt.value_expr),
                          "array literal shape mismatch: expected " +
                              shape_display(explicit_shape) + ", got [0]");
            }
            if (actual.status == ArrayShapeStatus::Inferred && actual.shape != explicit_shape) {
                sema_fail(
                    array_shape_mismatch_location(stmt.value_expr, explicit_shape, actual.shape),
                    "array literal shape mismatch: expected " + shape_display(explicit_shape) +
                        ", got " + shape_display(actual.shape));
            }
        }
        const EffectiveVarType type = effective_var_type(stmt, inferred);
        if (!type.inferred) {
            check_known_scoped_type_ref(scope, diagnostic_location(stmt.location, declared_type),
                                        declared_type, "unknown local type: ");
        } else {
            check_known_scoped_type_ref(scope, diagnostic_location(stmt.location, type.ref),
                                        type.ref, "unknown local type: ");
        }
        if (sema_has_expr(stmt.value_expr)) {
            if (inferred.status == ArrayShapeStatus::Inferred &&
                is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, inferred.element_type_ref, stmt.value_expr,
                                             diagnostic_location(stmt.location, stmt.value_expr));
            } else if (has_type_ref(explicit_element) && is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, explicit_element, stmt.value_expr,
                                             diagnostic_location(stmt.location, stmt.value_expr));
            } else if (!type.inferred) {
                check_type_ref_match(scope, type.ref, stmt.value_expr,
                                     diagnostic_location(stmt.location, stmt.value_expr));
            } else {
                check_type_ref_match(scope, type.ref, stmt.value_expr,
                                     diagnostic_location(stmt.location, stmt.value_expr));
            }
        }
        bind_local(scope, stmt.name, type.ref);
        if (is_dudu_all_caps(stmt.name)) {
            scope.constants.insert(stmt.name);
        }
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (const std::vector<std::string> names = tuple_binding_names(stmt_target_expr(stmt));
            !names.empty()) {
            const SourceLocation& value_location =
                diagnostic_location(stmt.location, stmt.value_expr);
            const std::vector<TypeRef> types = template_type_arg_refs_with_aliases(
                infer_expr_type_ast(scope, stmt.value_expr, &value_location), "tuple",
                scope.symbols.alias_type_refs);
            if (names.size() != types.size()) {
                sema_fail(value_location, "tuple destructuring count mismatch");
            }
            check_destructure_bindings(stmt.location, names, scope.local_type_refs);
            for (size_t i = 0; i < names.size(); ++i) {
                bind_local(scope, names[i], types[i]);
            }
            return;
        }
        if (stmt_target_expr(stmt).kind == ExprKind::TupleLiteral) {
            sema_fail(diagnostic_location(stmt.location, stmt_target_expr(stmt)),
                      "tuple destructuring targets must be names");
        }
        if (stmt_target_expr(stmt).kind == ExprKind::Name &&
            !scope.local_type_refs.contains(stmt_target_expr(stmt).name)) {
            reject_standalone_slice_value(stmt.value_expr);
            const std::string& name = stmt_target_expr(stmt).name;
            check_local_binding_name(diagnostic_location(stmt.location, stmt_target_expr(stmt)),
                                     name);
            const TypeRef inferred = infer_binding_type(
                scope, stmt.value_expr, diagnostic_location(stmt.location, stmt.value_expr));
            bind_local(scope, name, inferred);
            if (is_dudu_all_caps(name)) {
                scope.constants.insert(name);
            }
            return;
        }
        const TypeRef target_type = assignment_target_type_ref(scope, stmt);
        reject_standalone_slice_value(stmt.value_expr);
        if (has_type_ref(target_type)) {
            check_type_ref_match(scope, target_type, stmt.value_expr,
                                 diagnostic_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    (void)infer_expr_type_ast(scope, stmt.expr, &stmt.location);
}

void copy_base_scope_state(FunctionScope& dst, const FunctionScope& src) {
    dst.constants = src.constants;
    dst.target_mode = src.target_mode;
    dst.current_class = src.current_class;
    dst.allow_super_init = src.allow_super_init;
    dst.return_type_ref = src.return_type_ref;
    dst.local_type_refs = src.local_type_refs;
}

template <typename Action>
void check_body_region(std::vector<CompileError>* diagnostics, Action&& action) {
    try {
        action();
    } catch (const CompileError& error) {
        if (diagnostics == nullptr) {
            throw;
        }
        diagnostics->push_back(error);
    }
}

void check_bodies_impl(const ModuleAst& module, const Symbols& symbols,
                       std::vector<CompileError>* diagnostics) {
    FunctionScope base{symbols};
    Symbols scoped_symbols = symbols;
    const auto mode = module.build_values.find("TARGET_MODE");
    if (mode != module.build_values.end()) {
        base.target_mode = trim(mode->second);
        if (base.target_mode.size() >= 2 && base.target_mode.front() == '"' &&
            base.target_mode.back() == '"') {
            base.target_mode = base.target_mode.substr(1, base.target_mode.size() - 2);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        check_body_region(diagnostics, [&] {
            const ArrayShapeInference inferred =
                infer_array_literal_shape_type(constant.type_ref, constant.value_expr);
            if (inferred.status == ArrayShapeStatus::EmptyLiteral) {
                sema_fail(diagnostic_location(constant.location, constant.value_expr),
                          "array shape cannot be inferred from an empty literal");
            }
            if (inferred.status == ArrayShapeStatus::RaggedLiteral) {
                sema_fail(inferred.error_location.line > 0 ? inferred.error_location
                                                           : constant.location,
                          "ragged array literal");
            }
            const TypeRef constant_type = inferred.status == ArrayShapeStatus::Inferred
                                              ? inferred.type_ref
                                              : constant.type_ref;
            if (inferred.status == ArrayShapeStatus::Inferred &&
                is_array_literal(constant.value_expr)) {
                check_array_literal_elements(
                    base, inferred.element_type_ref, constant.value_expr,
                    diagnostic_location(constant.location, constant.value_expr));
            }
            bind_local(base, constant.name, constant_type);
            base.constants.insert(constant.name);
        });
    }
    for (const ClassDecl& klass : module.classes) {
        ScopedSymbolOverlay class_symbols(scoped_symbols);
        class_symbols.add_generic_params(klass.generic_params,
                                         generic_value_params_for_class(klass));
        class_symbols.set_self_type(klass.name);
        for (const FunctionDecl& method : klass.methods) {
            if (method.body_syntax_damaged || function_has_decorator(method, "abstract")) {
                continue;
            }
            check_body_region(diagnostics, [&] {
                ScopedSymbolOverlay method_symbols(scoped_symbols);
                method_symbols.add_generic_params(method.generic_params,
                                                  generic_value_params_for_function(method));
                FunctionScope scope{scoped_symbols};
                copy_base_scope_state(scope, base);
                scope.current_class = klass.name;
                scope.allow_super_init = method.name == "init";
                scope.return_type_ref = function_return_type_ref(method);
                for (const ParamDecl& param : method.params) {
                    bind_local(scope, param.name,
                               substitute_method_self_type(klass, param.type_ref, param.location));
                }
                const TypeRef return_type_ref = scope.return_type_ref;
                check_block(scope, method.statements, return_type_ref, 0);
                if (function_has_return_type(method) && !type_ref_is_void(return_type_ref) &&
                    !block_guarantees_return(method.statements)) {
                    sema_fail(method.location, "missing return in function: " + method.name);
                }
            });
        }
    }
    for (const EnumDecl& en : module.enums) {
        ScopedSymbolOverlay enum_symbols(scoped_symbols);
        enum_symbols.set_self_type(en.name);
        for (const FunctionDecl& method : en.methods) {
            if (method.body_syntax_damaged)
                continue;
            check_body_region(diagnostics, [&] {
                ScopedSymbolOverlay method_symbols(scoped_symbols);
                method_symbols.add_generic_params(method.generic_params,
                                                  generic_value_params_for_function(method));
                FunctionScope scope{scoped_symbols};
                copy_base_scope_state(scope, base);
                scope.current_class = en.name;
                const TypeRef self_type = named_type_ref(en.name, method.location);
                const auto substitute_self = [&](const TypeRef& type) {
                    return substitute_type_ref(type, {{"Self", self_type}});
                };
                scope.return_type_ref = substitute_self(function_return_type_ref(method));
                for (const ParamDecl& param : method.params) {
                    bind_local(scope, param.name, substitute_self(param.type_ref));
                }
                check_block(scope, method.statements, scope.return_type_ref, 0);
                if (function_has_return_type(method) && !type_ref_is_void(scope.return_type_ref) &&
                    !block_guarantees_return(method.statements)) {
                    sema_fail(method.location, "missing return in function: " + method.name);
                }
            });
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        if (fn.body_syntax_damaged) {
            continue;
        }
        check_body_region(diagnostics, [&] {
            ScopedSymbolOverlay function_symbols(scoped_symbols);
            function_symbols.add_generic_params(fn.generic_params,
                                                generic_value_params_for_function(fn));
            FunctionScope scope{scoped_symbols};
            copy_base_scope_state(scope, base);
            scope.return_type_ref = function_return_type_ref(fn);
            for (const ParamDecl& param : fn.params) {
                bind_local(scope, param.name, param.type_ref);
            }
            const TypeRef return_type_ref = function_return_type_ref(fn);
            check_block(scope, fn.statements, return_type_ref, 0);
            if (function_has_return_type(fn) && !type_ref_is_void(return_type_ref) &&
                !block_guarantees_return(fn.statements)) {
                sema_fail(fn.location, "missing return in function: " + fn.name);
            }
        });
    }
}

} // namespace

void check_function_body_statements(FunctionScope& scope, const std::vector<Stmt>& body,
                                    const TypeRef& return_type_ref) {
    check_block(scope, body, return_type_ref, 0);
}

void check_bodies(const ModuleAst& module, const Symbols& symbols) {
    check_bodies_impl(module, symbols, nullptr);
}

std::vector<CompileError> check_bodies_collecting(const ModuleAst& module, const Symbols& symbols) {
    std::vector<CompileError> diagnostics;
    check_bodies_impl(module, symbols, &diagnostics);
    return diagnostics;
}

} // namespace dudu
