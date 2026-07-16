#include "dudu/codegen/cpp_expr_index_hooks.hpp"

#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_emit_support.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace dudu {
namespace {

struct ResolvedIndexHook {
    IndexOperatorTarget operators;
    const Expr* receiver = nullptr;
    TypeRef receiver_type;
};

std::vector<Expr> index_arg_exprs(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        return index_expr.children;
    }
    return {index_expr};
}

std::vector<TypeRef> infer_index_arg_type_refs(
    const std::vector<Expr>& args, const std::map<std::string, TypeRef>& local_type_refs,
    const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols) {
    std::vector<TypeRef> out;
    out.reserve(args.size());
    for (const Expr& arg : args) {
        out.push_back(
            infer_emitted_local_type_ref(arg, local_type_refs, function_returns, symbols));
    }
    return out;
}

std::optional<ResolvedIndexHook>
resolve_index_hook(const Expr& indexed_receiver, const CppLocalContext& locals,
                   const std::map<std::string, TypeRef>& local_type_refs,
                   const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols) {
    if (symbols == nullptr) {
        return std::nullopt;
    }

    const IndexOperatorTarget operators = index_operator_target(indexed_receiver);
    const Expr& receiver = *operators.receiver;
    TypeRef receiver_type;
    if (receiver.kind == ExprKind::Name) {
        receiver_type = local_type_ref(local_type_refs, receiver.name, receiver.location);
    } else if (receiver.kind == ExprKind::Member) {
        receiver_type = member_expr_type_ref(*symbols, local_type_refs, nullptr, receiver, {},
                                             locals.current_class);
    }
    if (!has_type_ref(receiver_type)) {
        receiver_type =
            infer_emitted_local_type_ref(receiver, local_type_refs, function_returns, symbols);
    }
    if (!has_type_ref(receiver_type)) {
        return std::nullopt;
    }
    return ResolvedIndexHook{
        .operators = operators, .receiver = &receiver, .receiver_type = std::move(receiver_type)};
}

std::string lower_hook_call(const ResolvedIndexHook& target, std::string_view method,
                            const std::vector<std::string>& args,
                            const std::vector<std::string>& aliases, const CppLocalContext& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const Symbols* symbols, const CppEmitOptions& options) {
    const std::string receiver =
        lower_expr(*target.receiver, aliases, locals, local_type_refs, symbols, options);
    const std::string access = target.receiver_type.kind == TypeKind::Pointer ? "->" : ".";
    return receiver + access + std::string(method) + "(" + join_names(args) + ")";
}

std::vector<std::string> lower_args(const std::vector<Expr>& args,
                                    const std::vector<std::string>& aliases,
                                    const CppLocalContext& locals,
                                    const std::map<std::string, TypeRef>& local_type_refs,
                                    const Symbols* symbols, const CppEmitOptions& options) {
    std::vector<std::string> out;
    out.reserve(args.size());
    for (const Expr& arg : args) {
        out.push_back(lower_expr(arg, aliases, locals, local_type_refs, symbols, options));
    }
    return out;
}

} // namespace

std::optional<std::string>
lower_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                            const CppLocalContext& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const std::map<std::string, TypeRef>& function_returns,
                            const Symbols* symbols, const CppEmitOptions& options) {
    const Expr& target_expr = stmt_target_expr(stmt);
    if (target_expr.kind != ExprKind::Index || target_expr.children.size() != 2) {
        return std::nullopt;
    }
    const auto target =
        resolve_index_hook(target_expr.children[0], locals, local_type_refs, {}, symbols);
    if (!target) {
        return std::nullopt;
    }

    std::vector<Expr> args = index_arg_exprs(target_expr.children[1]);
    args.push_back(stmt.value_expr);
    const std::vector<TypeRef> arg_types =
        infer_index_arg_type_refs(args, local_type_refs, function_returns, symbols);
    const auto signature = dudu_operator_signature_for_args(
        *symbols, target->operators.write_operator, target->receiver_type, args, arg_types);
    std::optional<std::string> method = dudu_operator_method_name_for_args(
        *symbols, target->operators.write_operator, target->receiver_type, args, arg_types);
    if (!method) {
        std::vector<TypeRef> read_arg_types = infer_index_arg_type_refs(
            index_arg_exprs(target_expr.children[1]), local_type_refs, function_returns, symbols);
        if (const auto read_signature = dudu_operator_signature_for_arg_types(
                *symbols, target->operators.read_operator, target->receiver_type, read_arg_types)) {
            read_arg_types.push_back(signature_return_type_ref(*read_signature));
            method = dudu_operator_method_name_for_arg_types(
                *symbols, target->operators.write_operator, target->receiver_type, read_arg_types);
        }
    }
    if (!method) {
        return std::nullopt;
    }

    if (signature && signature->variadic &&
        signature_variadic_param_index(*signature) + 1 < signature_param_count(*signature) &&
        !args.empty()) {
        std::rotate(args.begin(), args.end() - 1, args.end());
    }
    return lower_hook_call(*target, *method,
                           lower_args(args, aliases, locals, local_type_refs, symbols, options),
                           aliases, locals, local_type_refs, symbols, options);
}

std::optional<std::string>
lower_compound_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                                     const CppLocalContext& locals,
                                     const std::map<std::string, TypeRef>& local_type_refs,
                                     const std::map<std::string, TypeRef>& function_returns,
                                     const Symbols* symbols, const CppEmitOptions& options) {
    const Expr& target_expr = stmt_target_expr(stmt);
    if (target_expr.kind != ExprKind::Index || target_expr.children.size() != 2) {
        return std::nullopt;
    }
    const auto target = resolve_index_hook(target_expr.children[0], locals, local_type_refs,
                                           function_returns, symbols);
    if (!target ||
        !lower_index_read_hook(target_expr, aliases, locals, local_type_refs, symbols, options)) {
        return std::nullopt;
    }

    const std::vector<Expr> args = index_arg_exprs(target_expr.children[1]);
    std::vector<TypeRef> arg_types =
        infer_index_arg_type_refs(args, local_type_refs, function_returns, symbols);
    const TypeRef indexed_type =
        infer_emitted_local_type_ref(target_expr, local_type_refs, function_returns, symbols);
    const Expr compound_value = compound_assignment_value_expr(stmt);
    const TypeRef compound_type =
        infer_emitted_local_type_ref(compound_value, local_type_refs, function_returns, symbols);
    if (!has_type_ref(compound_type)) {
        return std::nullopt;
    }

    TypeRef value_type = compound_type;
    arg_types.push_back(compound_type);
    auto signature = dudu_operator_signature_for_arg_types(
        *symbols, target->operators.write_operator, target->receiver_type, arg_types);
    auto method = dudu_operator_method_name_for_arg_types(
        *symbols, target->operators.write_operator, target->receiver_type, arg_types);
    if (!method && has_type_ref(indexed_type)) {
        arg_types.back() = indexed_type;
        value_type = indexed_type;
        signature = dudu_operator_signature_for_arg_types(
            *symbols, target->operators.write_operator, target->receiver_type, arg_types);
        method = dudu_operator_method_name_for_arg_types(*symbols, target->operators.write_operator,
                                                         target->receiver_type, arg_types);
    }
    if (!method) {
        return std::nullopt;
    }

    std::vector<std::string> lowered_args =
        lower_args(args, aliases, locals, local_type_refs, symbols, options);
    const std::string lowered_value =
        lower_expr_as_type_ref(value_type, compound_value, aliases, locals, local_type_refs,
                               function_returns, symbols, options);
    lowered_args.push_back(lowered_value);
    if (signature && signature->variadic &&
        signature_variadic_param_index(*signature) + 1 < signature_param_count(*signature) &&
        !lowered_args.empty()) {
        std::rotate(lowered_args.begin(), lowered_args.end() - 1, lowered_args.end());
    }
    return lower_hook_call(*target, *method, lowered_args, aliases, locals, local_type_refs,
                           symbols, options);
}

std::optional<std::string>
lower_index_read_hook(const Expr& expr, const std::vector<std::string>& aliases,
                      const CppLocalContext& locals,
                      const std::map<std::string, TypeRef>& local_type_refs, const Symbols* symbols,
                      const CppEmitOptions& options) {
    if (expr.kind != ExprKind::Index || expr.children.size() != 2) {
        return std::nullopt;
    }
    const auto target = resolve_index_hook(expr.children[0], locals, local_type_refs, {}, symbols);
    if (!target) {
        return std::nullopt;
    }

    const std::vector<Expr> args = index_arg_exprs(expr.children[1]);
    const auto method = dudu_operator_method_name_for_args(
        *symbols, target->operators.read_operator, target->receiver_type, args,
        infer_index_arg_type_refs(args, local_type_refs, {}, symbols));
    if (!method) {
        return std::nullopt;
    }
    return lower_hook_call(*target, *method,
                           lower_args(args, aliases, locals, local_type_refs, symbols, options),
                           aliases, locals, local_type_refs, symbols, options);
}

} // namespace dudu
