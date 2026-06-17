#include "dudu/sema_body.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/decorators.hpp"
#include "dudu/escapes.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_alloc.hpp"
#include "dudu/sema_assignment.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_match.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/sema_super.hpp"
#include "dudu/type_compat.hpp"

#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace dudu {

namespace {

bool freestanding_like(const FunctionScope& scope) {
    return scope.target_mode == "freestanding" || scope.target_mode == "embedded";
}

bool is_array_literal(const Expr& expr) {
    return expr.kind == ExprKind::ListLiteral;
}

bool function_has_decorator(const FunctionDecl& fn, std::string_view name) {
    return dudu::has_decorator(fn.decorators, name);
}

void check_type_match(FunctionScope& scope, const std::string& expected, const Expr& expr,
                      const SourceLocation& location, const BodyCheckCallbacks& callbacks,
                      std::string_view mismatch_label = {}) {
    if (expr.kind == ExprKind::Call && !expr.callee.empty() &&
        expr.callee.front().kind == ExprKind::Member && expr.callee.front().children.size() == 1) {
        const Expr& member = expr.callee.front();
        const Expr& receiver = member.children.front();
        const bool receiver_is_bare_path =
            receiver.kind == ExprKind::Name && !scope.locals.contains(receiver.name);
        if (!receiver_is_bare_path) {
            const TypeRef receiver_ref = callbacks.infer_expr_type(scope, receiver, &location);
            const std::string receiver_type = substitute_type_ref_text(receiver_ref, {});
            if (const auto signature = inferred_generic_method_signature_for_type(
                    scope, receiver_type, member.name, expr.children, expected, &location,
                    {.infer_expr_type =
                         [&](const FunctionScope& nested, const Expr& arg,
                             const SourceLocation* arg_location) {
                             return callbacks.infer_expr_type(nested, arg, arg_location);
                         },
                     .can_assign =
                         [&](const FunctionScope& nested, const std::string& nested_expected,
                             const Expr& value, const std::string& got) {
                             return callbacks.can_assign(nested, nested_expected, value, got);
                         }})) {
                callbacks.check_call_args(scope, scoped_call_callee_text(scope, expr, &location),
                                          *signature, expr.children, &location);
                if (callbacks.can_assign(scope, expected, expr, signature->return_type)) {
                    return;
                }
            }
        }
    }
    const TypeRef expected_ref = parse_type_text(expected, location);
    const TypeRef got_ref = callbacks.infer_expr_type(scope, expr, &location);
    const std::string got = substitute_type_ref_text(got_ref, {});
    if (!type_assignment_allowed(expected_ref, got_ref) &&
        !callbacks.can_assign(scope, expected, expr, got)) {
        if (!mismatch_label.empty()) {
            sema_fail(location,
                      std::string(mismatch_label) + ": expected " + expected + ", got " + got);
        }
        sema_fail(location, assignment_error(expected, expr, got));
    }
}

void check_type_ref_match(FunctionScope& scope, const TypeRef& expected, const Expr& expr,
                          const SourceLocation& location, const BodyCheckCallbacks& callbacks,
                          std::string_view mismatch_label = {}) {
    const std::string expected_text = substitute_type_ref_text(expected, {});
    if (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) {
        check_type_match(scope, expected_text, expr, location, callbacks, mismatch_label);
        return;
    }
    const TypeRef got_ref = callbacks.infer_expr_type(scope, expr, &location);
    const std::string got = substitute_type_ref_text(got_ref, {});
    if (!type_assignment_allowed(expected, got_ref) &&
        !assignment_type_allowed(expected, expr, got) &&
        !callbacks.can_assign(scope, expected_text, expr, got)) {
        if (!mismatch_label.empty()) {
            sema_fail(location,
                      std::string(mismatch_label) + ": expected " + expected_text + ", got " + got);
        }
        sema_fail(location, assignment_error(expected, expr, got));
    }
}

void check_array_literal_elements(FunctionScope& scope, const TypeRef& element_type,
                                  const Expr& expr, const SourceLocation& location,
                                  const BodyCheckCallbacks& callbacks) {
    if (expr.kind != ExprKind::ListLiteral) {
        const TypeRef got_ref = callbacks.infer_expr_type(scope, expr, &location);
        const std::string expected_text = substitute_type_ref_text(element_type, {});
        const std::string got = substitute_type_ref_text(got_ref, {});
        if (!type_assignment_allowed(element_type, got_ref) &&
            !callbacks.can_assign(scope, expected_text, expr, got)) {
            sema_fail(location, "array literal element expects " + expected_text + ", got " + got);
        }
        return;
    }
    for (const Expr& child : expr.children) {
        check_array_literal_elements(scope, element_type, child, node_location(location, child),
                                     callbacks);
    }
}

struct EffectiveVarType {
    std::string text;
    TypeRef ref;
    bool inferred = false;
};

EffectiveVarType effective_var_type(const Stmt& stmt, const ArrayShapeInference& inferred) {
    if (inferred.status == ArrayShapeStatus::Inferred) {
        return {.text = inferred.type, .ref = inferred.type_ref, .inferred = true};
    }
    return {.text = substitute_type_ref_text(stmt.type_ref, {}), .ref = stmt.type_ref};
}

std::string shape_text(const std::vector<size_t>& shape) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << shape[i];
    }
    out << "]";
    return out.str();
}

TypeRef const_reference_type_ref(TypeRef type) {
    TypeRef const_type;
    const_type.kind = TypeKind::Const;
    const_type.text = "const[" + substitute_type_ref_text(type, {}) + "]";
    const_type.name = "const";
    const_type.children.push_back(std::move(type));

    TypeRef ref_type;
    ref_type.kind = TypeKind::Reference;
    ref_type.text = "&" + const_type.text;
    ref_type.children.push_back(std::move(const_type));
    return ref_type;
}

void check_condition_type(FunctionScope& scope, const Stmt& stmt,
                          const BodyCheckCallbacks& callbacks) {
    const SourceLocation& location = node_location(stmt.location, stmt.condition_expr);
    const TypeRef got_ref = callbacks.infer_expr_type(scope, stmt.condition_expr, &location);
    const std::string got = substitute_type_ref_text(got_ref, {});
    if (!got.empty() && got != "bool" && got != "auto") {
        if (const auto signature = dudu_operator_signature(scope.symbols, "bool", got);
            signature && signature->params.empty() && signature->return_type == "bool") {
            return;
        }
        sema_fail(location, "condition must be bool, got " + got);
    }
}

std::optional<TypeRef> infer_for_binding_type(FunctionScope& scope, const Stmt& stmt,
                                              const BodyCheckCallbacks& callbacks) {
    if (!sema_has_expr(stmt.iterable_expr)) {
        return std::nullopt;
    }
    const SourceLocation& location = node_location(stmt.location, stmt.iterable_expr);
    if (stmt.iterable_expr.kind == ExprKind::Call && stmt.iterable_expr.name == "range") {
        for (const Expr& arg : stmt.iterable_expr.children) {
            (void)callbacks.infer_expr_type(scope, arg, &location);
        }
        return parse_type_text("i32", location);
    }
    if (stmt.iterable_expr.kind == ExprKind::Name) {
        const std::string element = iterable_value_type(
            scope.symbols, scope.locals, scope.local_type_refs, stmt.iterable_expr.name);
        if (!element.empty()) {
            return parse_type_text(element, location);
        }
    }
    const TypeRef iterable_type = callbacks.infer_expr_type(scope, stmt.iterable_expr, &location);
    if (!has_type_ref(iterable_type)) {
        return std::nullopt;
    }
    if (const auto element = iterable_type_from_type(iterable_type)) {
        return parse_type_text(*element, location);
    }
    return std::nullopt;
}

void check_stmt(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                int loop_depth, const BodyCheckCallbacks& callbacks);

void check_block(FunctionScope& scope, const std::vector<Stmt>& body,
                 const std::string& return_type, int loop_depth,
                 const BodyCheckCallbacks& callbacks) {
    const bool allow_super_init_at_start = scope.allow_super_init;
    for (size_t i = 0; i < body.size(); ++i) {
        const Stmt& stmt = body[i];
        scope.allow_super_init =
            allow_super_init_at_start && i == 0 && loop_depth == 0 && is_super_init_stmt(stmt);
        check_stmt(scope, stmt, return_type, loop_depth, callbacks);
    }
    scope.allow_super_init = false;
}

void check_stmt(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                int loop_depth, const BodyCheckCallbacks& callbacks) {
    check_local_address_escape(stmt, scope.locals);
    if (stmt.kind == StmtKind::Return) {
        const SourceLocation& value_location = node_location(stmt.location, stmt.value_expr);
        if (missing_expr(stmt.value_expr)) {
            if (return_type != "void") {
                sema_fail(value_location,
                          "return type mismatch: expected " + return_type + ", got void");
            }
            return;
        }
        if (return_type != "void") {
            if (has_type_ref(scope.return_type_ref)) {
                check_type_ref_match(scope, scope.return_type_ref, stmt.value_expr, value_location,
                                     callbacks, "return type mismatch");
            } else {
                check_type_match(scope, return_type, stmt.value_expr, value_location, callbacks,
                                 "return type mismatch");
            }
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
            scope, stmt, return_type, loop_depth,
            {.infer_expr_type = callbacks.infer_expr_type,
             .check_block = [&](FunctionScope& nested, const std::vector<Stmt>& body,
                                const std::string& nested_return_type, int nested_loop_depth) {
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
        check_block(scope, stmt.children, return_type, loop_depth, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::Else || stmt.kind == StmtKind::Try) {
        check_block(scope, stmt.children, return_type, loop_depth, callbacks);
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
            bind_local(nested, stmt.name, "&const[" + stmt.type + "]",
                       const_reference_type_ref(stmt.type_ref));
        }
        check_block(nested, stmt.children, return_type, loop_depth, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::While) {
        check_condition_type(scope, stmt, callbacks);
        check_block(scope, stmt.children, return_type, loop_depth + 1, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::For) {
        FunctionScope nested = scope;
        if (!stmt.name.empty() && sema_has_expr(stmt.iterable_expr)) {
            check_local_binding_name(stmt.location, stmt.name);
            TypeRef binding_type = stmt.type_ref;
            if (stmt.type.empty()) {
                const auto inferred = infer_for_binding_type(scope, stmt, callbacks);
                if (!inferred) {
                    sema_fail(node_location(stmt.location, stmt.iterable_expr),
                              "cannot infer loop binding type");
                }
                binding_type = *inferred;
            } else {
                check_known_type_ref(scope.symbols, node_location(stmt.location, stmt.type_ref),
                                     stmt.type_ref, "unknown loop binding type: ");
                check_iterable_binding(scope.symbols, scope.locals, scope.local_type_refs,
                                       node_location(stmt.location, stmt.iterable_expr),
                                       binding_type, stmt.iterable_expr);
            }
            bind_local(nested, stmt.name, substitute_type_ref_text(binding_type, {}), binding_type);
        }
        check_block(nested, stmt.children, return_type, loop_depth + 1, callbacks);
        return;
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        const std::string target_type = assignment_target_type(scope, stmt, callbacks);
        if (!target_type.empty()) {
            check_type_match(scope, target_type, stmt.value_expr,
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
        } else if (!known_type(scope.symbols, type.text)) {
            sema_fail(node_location(stmt.location, stmt.type_ref),
                      "unknown local type: " + type.text);
        }
        if (sema_has_expr(stmt.value_expr)) {
            if (inferred.status == ArrayShapeStatus::Inferred &&
                is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, stmt.type_ref.children.front(), stmt.value_expr,
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
        bind_local(scope, stmt.name, type.text, type.ref);
        if (is_dudu_all_caps(stmt.name)) {
            scope.constants.insert(stmt.name);
        }
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (const std::vector<std::string> names = tuple_binding_names(stmt.target_expr);
            !names.empty()) {
            const SourceLocation& value_location = node_location(stmt.location, stmt.value_expr);
            const std::vector<TypeRef> types = template_type_arg_refs_resolved(
                callbacks.infer_expr_type(scope, stmt.value_expr, &value_location), "tuple",
                scope.symbols.aliases);
            if (names.size() != types.size()) {
                sema_fail(value_location, "tuple destructuring count mismatch");
            }
            check_destructure_bindings(stmt.location, names, scope.locals);
            for (size_t i = 0; i < names.size(); ++i) {
                bind_local(scope, names[i], substitute_type_ref_text(types[i], {}), types[i]);
            }
            return;
        }
        if (stmt.target_expr.kind == ExprKind::TupleLiteral) {
            sema_fail(node_location(stmt.location, stmt.target_expr),
                      "tuple destructuring targets must be names");
        }
        if (stmt.target_expr.kind == ExprKind::Name &&
            !scope.locals.contains(stmt.target_expr.name)) {
            const std::string& name = stmt.target_expr.name;
            check_local_binding_name(node_location(stmt.location, stmt.target_expr), name);
            const TypeRef inferred = callbacks.infer_expr_type(
                scope, stmt.value_expr, &node_location(stmt.location, stmt.value_expr));
            const std::string inferred_text = substitute_type_ref_text(inferred, {});
            bind_local(scope, name, inferred_text.empty() ? "auto" : inferred_text, inferred);
            if (is_dudu_all_caps(name)) {
                scope.constants.insert(name);
            }
            return;
        }
        const std::string target_type = assignment_target_type(scope, stmt, callbacks);
        if (!target_type.empty()) {
            check_type_match(scope, target_type, stmt.value_expr,
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
    dst.locals = src.locals;
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
        bind_local(base, constant.name, constant.type, constant.type_ref);
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
            scope.return_type_ref = method.return_type.empty()
                                        ? parse_type_text("void", method.location)
                                        : method.return_type_ref;
            for (const ParamDecl& param : method.params) {
                bind_local(scope, param.name, param.type, param.type_ref);
            }
            check_block(scope, method.statements,
                        method.return_type.empty() ? "void" : method.return_type, 0, callbacks);
            if (!method.return_type.empty() && method.return_type != "void" &&
                !block_guarantees_return(method.statements)) {
                sema_fail(method.location, "missing return in function: " + method.name);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        Symbols function_symbols = with_generic_params(symbols, fn.generic_params);
        FunctionScope scope{function_symbols};
        copy_base_scope_state(scope, base);
        scope.return_type_ref =
            fn.return_type.empty() ? parse_type_text("void", fn.location) : fn.return_type_ref;
        for (const ParamDecl& param : fn.params) {
            bind_local(scope, param.name, param.type, param.type_ref);
        }
        check_block(scope, fn.statements, fn.return_type.empty() ? "void" : fn.return_type, 0,
                    callbacks);
        if (!fn.return_type.empty() && fn.return_type != "void" &&
            !block_guarantees_return(fn.statements)) {
            sema_fail(fn.location, "missing return in function: " + fn.name);
        }
    }
}

} // namespace dudu
