#include "dudu/sema_body.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/escapes.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_alloc.hpp"
#include "dudu/sema_assignment.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_body_helpers.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_match.hpp"
#include "dudu/sema_super.hpp"

namespace dudu {

namespace {

void check_stmt(FunctionScope& scope, const Stmt& stmt, const TypeRef& return_type_ref,
                int loop_depth, const BodyCheckCallbacks& callbacks);

void check_block(FunctionScope& scope, const std::vector<Stmt>& body,
                 const TypeRef& return_type_ref, int loop_depth,
                 const BodyCheckCallbacks& callbacks) {
    const bool allow_super_init_at_start = scope.allow_super_init;
    for (size_t i = 0; i < body.size(); ++i) {
        const Stmt& stmt = body[i];
        scope.allow_super_init =
            allow_super_init_at_start && i == 0 && loop_depth == 0 && is_super_init_stmt(stmt);
        check_stmt(scope, stmt, return_type_ref, loop_depth, callbacks);
    }
    scope.allow_super_init = false;
}

void check_stmt(FunctionScope& scope, const Stmt& stmt, const TypeRef& return_type_ref,
                int loop_depth, const BodyCheckCallbacks& callbacks) {
    check_local_address_escape(stmt, scope.local_type_refs);
    const std::string return_type = substitute_type_ref_text(return_type_ref, {});
    if (stmt.kind == StmtKind::Return) {
        const SourceLocation& value_location = node_location(stmt.location, stmt.value_expr);
        if (missing_expr(stmt.value_expr)) {
            if (!type_ref_is_void(return_type_ref)) {
                sema_fail(value_location,
                          "return type mismatch: expected " + return_type + ", got void");
            }
            return;
        }
        if (!type_ref_is_void(return_type_ref)) {
            check_type_ref_match(scope, return_type_ref, stmt.value_expr, value_location, callbacks,
                                 "return type mismatch");
            return;
        }
        const TypeRef got_ref = callbacks.infer_expr_type(scope, stmt.value_expr, &value_location);
        const std::string got = substitute_type_ref_text(got_ref, {});
        if (got != "void") {
            sema_fail(value_location, "void function cannot return " + got);
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
        check_condition_type(scope, stmt, callbacks);
        if (sema_has_expr(stmt.message_expr)) {
            (void)callbacks.infer_expr_type(scope, stmt.message_expr,
                                            &node_location(stmt.location, stmt.message_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::Raise) {
        if (sema_has_expr(stmt.value_expr)) {
            (void)callbacks.infer_expr_type(scope, stmt.value_expr,
                                            &node_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::Match) {
        check_match_stmt(
            scope, stmt, return_type_ref, loop_depth,
            {.infer_expr_type = callbacks.infer_expr_type,
             .check_block = [&](FunctionScope& nested, const std::vector<Stmt>& body,
                                const TypeRef& nested_return_type, int nested_loop_depth) {
                 check_block(nested, body, nested_return_type, nested_loop_depth, callbacks);
             }});
        return;
    }
    if (stmt.kind == StmtKind::Case) {
        sema_fail(stmt.location, "case outside match");
    }
    if (stmt.kind == StmtKind::Delete) {
        std::vector<TypeRef> arg_types;
        auto infer_type = [&](const Expr& expr) {
            return callbacks.infer_expr_type(scope, expr, &node_location(stmt.location, expr));
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
        check_condition_type(scope, stmt, callbacks);
        check_block(scope, stmt.children, return_type_ref, loop_depth, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::Else || stmt.kind == StmtKind::Try) {
        check_block(scope, stmt.children, return_type_ref, loop_depth, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        FunctionScope nested = scope;
        if (sema_has_expr(stmt.condition_expr) ||
            (stmt.name.empty() != !has_type_ref(stmt.type_ref))) {
            sema_fail(stmt.location, "expected except binding as name: Type");
        }
        if (!stmt.name.empty()) {
            check_local_binding_name(stmt.location, stmt.name);
            check_known_type_ref(scope.symbols, node_location(stmt.location, stmt.type_ref),
                                 stmt.type_ref, "unknown catch type: ");
            TypeRef catch_type = const_reference_type_ref(stmt.type_ref);
            bind_local(nested, stmt.name, catch_type);
        }
        check_block(nested, stmt.children, return_type_ref, loop_depth, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::While) {
        check_condition_type(scope, stmt, callbacks);
        check_block(scope, stmt.children, return_type_ref, loop_depth + 1, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::For) {
        FunctionScope nested = scope;
        if (!stmt.name.empty() && sema_has_expr(stmt.iterable_expr)) {
            check_local_binding_name(stmt.location, stmt.name);
            TypeRef binding_type = stmt.type_ref;
            if (!has_type_ref(binding_type)) {
                const auto inferred = infer_for_binding_type(scope, stmt, callbacks);
                if (!inferred) {
                    sema_fail(node_location(stmt.location, stmt.iterable_expr),
                              "cannot infer loop binding type");
                }
                binding_type = *inferred;
            } else {
                check_known_type_ref(scope.symbols, node_location(stmt.location, stmt.type_ref),
                                     stmt.type_ref, "unknown loop binding type: ");
                check_iterable_binding(scope.symbols, scope.local_type_refs,
                                       node_location(stmt.location, stmt.iterable_expr),
                                       binding_type, stmt.iterable_expr);
            }
            bind_local(nested, stmt.name, binding_type);
        }
        check_block(nested, stmt.children, return_type_ref, loop_depth + 1, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        const TypeRef target_type = assignment_target_type_ref(scope, stmt, callbacks);
        if (has_type_ref(target_type)) {
            check_type_ref_match(scope, target_type, stmt.value_expr,
                                 node_location(stmt.location, stmt.value_expr), callbacks);
        }
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        check_local_binding_name(stmt.location, stmt.name);
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(stmt.type_ref, stmt.value_expr);
        if (inferred.status == ArrayShapeStatus::EmptyLiteral) {
            sema_fail(node_location(stmt.location, stmt.value_expr),
                      "array shape cannot be inferred from an empty literal");
        }
        if (inferred.status == ArrayShapeStatus::RaggedLiteral) {
            sema_fail(node_location(stmt.location, stmt.value_expr), "ragged array literal");
        }
        const std::vector<size_t> explicit_shape = explicit_array_shape(stmt.type_ref);
        const TypeRef explicit_element = explicit_array_element_type_ref(stmt.type_ref);
        if (!explicit_shape.empty() && stmt.value_expr.kind == ExprKind::ListLiteral) {
            const ArrayShapeInference actual =
                infer_array_literal_shape_type(stmt.type_ref.children.front(), stmt.value_expr);
            if (actual.status == ArrayShapeStatus::RaggedLiteral) {
                sema_fail(node_location(stmt.location, stmt.value_expr), "ragged array literal");
            }
            if (actual.status == ArrayShapeStatus::EmptyLiteral &&
                explicit_shape != std::vector<size_t>{0}) {
                sema_fail(node_location(stmt.location, stmt.value_expr),
                          "array literal shape mismatch: expected " + shape_text(explicit_shape) +
                              ", got [0]");
            }
            if (actual.status == ArrayShapeStatus::Inferred && actual.shape != explicit_shape) {
                sema_fail(node_location(stmt.location, stmt.value_expr),
                          "array literal shape mismatch: expected " + shape_text(explicit_shape) +
                              ", got " + shape_text(actual.shape));
            }
        }
        const EffectiveVarType type = effective_var_type(stmt, inferred);
        if (!type.inferred) {
            check_known_type_ref(scope.symbols, node_location(stmt.location, stmt.type_ref),
                                 stmt.type_ref, "unknown local type: ");
        } else {
            check_known_type_ref(scope.symbols, node_location(stmt.location, type.ref), type.ref,
                                 "unknown local type: ");
        }
        if (sema_has_expr(stmt.value_expr)) {
            if (inferred.status == ArrayShapeStatus::Inferred &&
                is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, inferred.element_type_ref, stmt.value_expr,
                                             node_location(stmt.location, stmt.value_expr),
                                             callbacks);
            } else if (has_type_ref(explicit_element) && is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, explicit_element, stmt.value_expr,
                                             node_location(stmt.location, stmt.value_expr),
                                             callbacks);
            } else if (!type.inferred) {
                check_type_ref_match(scope, type.ref, stmt.value_expr,
                                     node_location(stmt.location, stmt.value_expr), callbacks);
            } else {
                check_type_ref_match(scope, type.ref, stmt.value_expr,
                                     node_location(stmt.location, stmt.value_expr), callbacks);
            }
        }
        bind_local(scope, stmt.name, type.ref);
        if (is_dudu_all_caps(stmt.name)) {
            scope.constants.insert(stmt.name);
        }
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (const std::vector<std::string> names = tuple_binding_names(stmt.target_expr);
            !names.empty()) {
            const SourceLocation& value_location = node_location(stmt.location, stmt.value_expr);
            const std::vector<TypeRef> types = template_type_arg_refs_with_aliases(
                callbacks.infer_expr_type(scope, stmt.value_expr, &value_location), "tuple",
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
        if (stmt.target_expr.kind == ExprKind::TupleLiteral) {
            sema_fail(node_location(stmt.location, stmt.target_expr),
                      "tuple destructuring targets must be names");
        }
        if (stmt.target_expr.kind == ExprKind::Name &&
            !scope.local_type_refs.contains(stmt.target_expr.name)) {
            const std::string& name = stmt.target_expr.name;
            check_local_binding_name(node_location(stmt.location, stmt.target_expr), name);
            const TypeRef inferred = callbacks.infer_expr_type(
                scope, stmt.value_expr, &node_location(stmt.location, stmt.value_expr));
            bind_local(scope, name, inferred);
            if (is_dudu_all_caps(name)) {
                scope.constants.insert(name);
            }
            return;
        }
        const TypeRef target_type = assignment_target_type_ref(scope, stmt, callbacks);
        if (has_type_ref(target_type)) {
            check_type_ref_match(scope, target_type, stmt.value_expr,
                                 node_location(stmt.location, stmt.value_expr), callbacks);
        }
        return;
    }
    (void)callbacks.infer_expr_type(scope, stmt.expr, &stmt.location);
}

Symbols with_generic_params(Symbols symbols, const std::vector<std::string>& params) {
    for (const std::string& param : params) {
        symbols.types.insert(param);
        symbols.generic_params.insert(param);
    }
    return symbols;
}

void copy_base_scope_state(FunctionScope& dst, const FunctionScope& src) {
    dst.constants = src.constants;
    dst.target_mode = src.target_mode;
    dst.current_class = src.current_class;
    dst.allow_super_init = src.allow_super_init;
    dst.return_type_ref = src.return_type_ref;
    dst.local_type_refs = src.local_type_refs;
}

} // namespace

void check_bodies(const ModuleAst& module, const Symbols& symbols,
                  const BodyCheckCallbacks& callbacks) {
    FunctionScope base{symbols};
    const auto mode = module.build_values.find("TARGET_MODE");
    if (mode != module.build_values.end()) {
        base.target_mode = trim(mode->second);
        if (base.target_mode.size() >= 2 && base.target_mode.front() == '"' &&
            base.target_mode.back() == '"') {
            base.target_mode = base.target_mode.substr(1, base.target_mode.size() - 2);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        bind_local(base, constant.name, constant.type_ref);
        base.constants.insert(constant.name);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (function_has_decorator(method, "abstract")) {
                continue;
            }
            Symbols method_symbols = with_generic_params(symbols, klass.generic_params);
            method_symbols = with_generic_params(method_symbols, method.generic_params);
            FunctionScope scope{method_symbols};
            copy_base_scope_state(scope, base);
            scope.current_class = klass.name;
            scope.allow_super_init = method.name == "init";
            scope.return_type_ref = function_return_type_ref(method);
            for (const ParamDecl& param : method.params) {
                bind_local(scope, param.name, param.type_ref);
            }
            const TypeRef return_type_ref = function_return_type_ref(method);
            const std::string return_type = substitute_type_ref_text(return_type_ref, {});
            check_block(scope, method.statements, return_type_ref, 0, callbacks);
            if (function_has_return_type(method) && !type_ref_is_void(return_type_ref) &&
                !block_guarantees_return(method.statements)) {
                sema_fail(method.location, "missing return in function: " + method.name);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        Symbols function_symbols = with_generic_params(symbols, fn.generic_params);
        FunctionScope scope{function_symbols};
        copy_base_scope_state(scope, base);
        scope.return_type_ref = function_return_type_ref(fn);
        for (const ParamDecl& param : fn.params) {
            bind_local(scope, param.name, param.type_ref);
        }
        const TypeRef return_type_ref = function_return_type_ref(fn);
        check_block(scope, fn.statements, return_type_ref, 0, callbacks);
        if (function_has_return_type(fn) && !type_ref_is_void(return_type_ref) &&
            !block_guarantees_return(fn.statements)) {
            sema_fail(fn.location, "missing return in function: " + fn.name);
        }
    }
}

} // namespace dudu
