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
lower_expected_generic_method_call(const std::string& expected_type, const Expr& expr,
                                   const std::vector<std::string>& aliases,
                                   const std::map<std::string, std::string>& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const std::map<std::string, TypeRef>& function_returns,
                                   const Symbols* symbols, const CppEmitOptions& options) {
    if (symbols == nullptr || expected_type.empty() || expr.kind != ExprKind::Call ||
        expr.callee.empty() || expr.callee.front().kind != ExprKind::Member ||
        expr.callee.front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr.callee.front();
    const Expr& receiver = member.children.front();
    std::string receiver_type =
        infer_emitted_local_type(receiver, locals, local_type_refs, function_returns);
    if (receiver_type.empty()) {
        receiver_type = member_expr_type(*symbols, locals, nullptr, receiver);
    }
    if (receiver_type.empty() || receiver_type == "auto") {
        return std::nullopt;
    }
    std::vector<TypeRef> arg_types;
    arg_types.reserve(expr.children.size());
    for (const Expr& arg : expr.children) {
        if (arg.kind == ExprKind::Name) {
            if (const auto local_type = local_type_refs.find(arg.name);
                local_type != local_type_refs.end()) {
                arg_types.push_back(local_type->second);
                continue;
            }
        }
        const std::string arg_type =
            infer_emitted_local_type(arg, locals, local_type_refs, function_returns);
        if (arg_type.empty() || arg_type == "auto") {
            return std::nullopt;
        }
        arg_types.push_back(parse_type_text(arg_type, arg.location));
    }
    const auto type_args =
        infer_expected_method_type_args(*symbols, receiver_type, member.name, arg_types,
                                        parse_type_text(expected_type, expr.location));
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
    return lower_callee_expr(expr, aliases, locals, symbols, options) + "<" + lowered_args.str() +
           ">(" + join_lowered_exprs(expr.children, aliases, locals, ", ", symbols, options) + ")";
}

} // namespace dudu
