#include "dudu/cpp_stmt_generic_methods.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_expr_call_emit.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_generics.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_methods_internal.hpp"

#include <sstream>

namespace dudu {
namespace {

std::optional<std::vector<TypeRef>> infer_expected_method_type_args(
    const Symbols& symbols, const std::string& receiver_type, const std::string& method_name,
    const std::vector<TypeRef>& arg_types, const TypeRef& expected_type) {
    const std::string type = unwrap_receiver_type(symbols, receiver_type);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name || method.generic_params.empty()) {
            continue;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        return infer_generic_method_type_args_from_type_refs(
            method, type + "." + method_name, arg_types, first_param, expected_type, nullptr);
    }
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        const std::string base = type_ref_text(base_decl.type_ref);
        if (const auto inferred = infer_expected_method_type_args(symbols, base, method_name,
                                                                  arg_types, expected_type)) {
            return inferred;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string>
lower_expected_generic_method_call(const TypeRef& expected_type, const Expr& expr,
                                   const std::vector<std::string>& aliases,
                                   const std::map<std::string, std::string>& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const std::map<std::string, TypeRef>& function_returns,
                                   const Symbols* symbols, const CppEmitOptions& options) {
    if (symbols == nullptr || !has_type_ref(expected_type) || expr.kind != ExprKind::Call ||
        expr.callee.empty() || expr.callee.front().kind != ExprKind::Member ||
        expr.callee.front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr.callee.front();
    const Expr& receiver = member.children.front();
    TypeRef receiver_type_ref =
        infer_emitted_local_type_ref(receiver, local_type_refs, function_returns, symbols);
    if (!has_type_ref(receiver_type_ref)) {
        receiver_type_ref =
            member_expr_type_ref(*symbols, locals, local_type_refs, nullptr, receiver);
    }
    const std::string receiver_type =
        has_type_ref(receiver_type_ref) ? type_ref_text(receiver_type_ref) : std::string{};
    if (receiver_type.empty() || receiver_type == "auto") {
        return std::nullopt;
    }
    std::vector<TypeRef> arg_types;
    arg_types.reserve(expr.children.size());
    for (const Expr& arg : expr.children) {
        const TypeRef arg_type =
            infer_emitted_local_type_ref(arg, local_type_refs, function_returns, symbols);
        if (!has_type_ref(arg_type) || type_ref_text(arg_type) == "auto") {
            return std::nullopt;
        }
        arg_types.push_back(arg_type);
    }
    const auto type_args = infer_expected_method_type_args(*symbols, receiver_type, member.name,
                                                           arg_types, expected_type);
    if (!type_args) {
        return std::nullopt;
    }
    std::ostringstream lowered_args;
    for (size_t i = 0; i < type_args->size(); ++i) {
        if (i > 0) {
            lowered_args << ", ";
        }
        lowered_args << lower_cpp_type((*type_args)[i], aliases, options);
    }
    return lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options) + "<" +
           lowered_args.str() + ">(" +
           join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                              options) +
           ")";
}

} // namespace dudu
