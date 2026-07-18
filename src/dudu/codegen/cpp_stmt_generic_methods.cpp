#include "dudu/codegen/cpp_stmt_generic_methods.hpp"

#include "dudu/codegen/cpp_expr_call_emit.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/type_compat.hpp"

#include <algorithm>
#include <sstream>

namespace dudu {
namespace {

std::optional<std::vector<TypeRef>> infer_expected_method_type_args(
    const Symbols& symbols, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<TypeRef>& arg_types, const TypeRef& expected_type) {
    const std::string type = receiver_class_name(symbols, receiver_type);
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
        if (const auto inferred = infer_expected_method_type_args(
                symbols, base_decl.type_ref, method_name, arg_types, expected_type)) {
            return inferred;
        }
    }
    return std::nullopt;
}

bool concrete_method_matches_expected(const Symbols& symbols, const TypeRef& receiver_type,
                                      const std::string& method_name,
                                      const std::vector<TypeRef>& arg_types,
                                      const TypeRef& expected_type) {
    for (const FunctionSignature& signature :
         method_signatures_for_type(symbols, receiver_type, method_name)) {
        if (signature_param_count(signature) != arg_types.size()) {
            continue;
        }
        bool args_match = true;
        for (size_t i = 0; i < arg_types.size(); ++i) {
            if (!type_assignment_allowed(signature_param_type_ref(signature, i), arg_types[i])) {
                args_match = false;
                break;
            }
        }
        if (args_match &&
            type_assignment_allowed(expected_type, signature_return_type_ref(signature))) {
            return true;
        }
    }
    return false;
}

bool depends_on_local_type(const TypeRef& type, const CppLocalContext& locals,
                           const Symbols* symbols) {
    if (!has_type_ref(type)) {
        return false;
    }
    if (locals.contains_type(type_ref_head_name(type))) {
        return true;
    }
    if (symbols != nullptr &&
        (type_ref_head_name(type) == "Self" || type_ref_head_name(type) == locals.current_class)) {
        const auto current = symbols->classes.find(locals.current_class);
        if (current != symbols->classes.end() && !current->second->generic_params.empty()) {
            return true;
        }
    }
    return std::ranges::any_of(type.children, [&](const TypeRef& child) {
        return depends_on_local_type(child, locals, symbols);
    });
}

std::string method_template_callee(const Expr& expr, const TypeRef& receiver_type,
                                   const std::vector<std::string>& aliases,
                                   const CppLocalContext& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const Symbols* symbols, const CppEmitOptions& options) {
    std::string callee =
        lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options);
    bool dependent = depends_on_local_type(receiver_type, locals, symbols);
    if (!dependent && symbols != nullptr && has_expr_callee(expr) &&
        expr_callee(expr).front().kind == ExprKind::Member &&
        expr_callee(expr).front().children.size() == 1 &&
        expr_callee(expr).front().children.front().kind == ExprKind::Name &&
        expr_callee(expr).front().children.front().name == "self") {
        const ClassDecl* receiver_class = class_for_receiver_type(*symbols, receiver_type);
        dependent = receiver_class != nullptr && !receiver_class->generic_params.empty();
    }
    if (!dependent) {
        return callee;
    }
    const size_t arrow = callee.rfind("->");
    const size_t dot = callee.rfind('.');
    const size_t separator = arrow == std::string::npos ? dot
                             : dot == std::string::npos ? arrow
                                                        : std::max(arrow, dot);
    if (separator == std::string::npos) {
        return callee;
    }
    callee.insert(separator + (separator == arrow ? 2 : 1), "template ");
    return callee;
}

} // namespace

std::optional<std::string> lower_expected_generic_method_call(
    const TypeRef& expected_type, const Expr& expr, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols,
    const CppEmitOptions& options) {
    if (symbols == nullptr || !has_type_ref(expected_type) || expr.kind != ExprKind::Call ||
        !has_expr_callee(expr) || expr_callee(expr).front().kind != ExprKind::Member ||
        expr_callee(expr).front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr_callee(expr).front();
    const Expr& receiver = member.children.front();
    TypeRef receiver_type_ref =
        infer_emitted_local_type_ref(receiver, local_type_refs, function_returns, symbols);
    if (!has_type_ref(receiver_type_ref)) {
        receiver_type_ref = member_expr_type_ref(*symbols, local_type_refs, nullptr, receiver);
    }
    if (!has_type_ref(receiver_type_ref) || type_ref_is_auto(receiver_type_ref)) {
        return std::nullopt;
    }
    std::vector<TypeRef> arg_types;
    arg_types.reserve(expr.children.size());
    for (const Expr& arg : expr.children) {
        const TypeRef arg_type =
            infer_emitted_local_type_ref(arg, local_type_refs, function_returns, symbols);
        if (!has_type_ref(arg_type) || type_ref_is_auto(arg_type)) {
            return std::nullopt;
        }
        arg_types.push_back(arg_type);
    }
    if (concrete_method_matches_expected(*symbols, receiver_type_ref, member.name, arg_types,
                                         expected_type)) {
        return std::nullopt;
    }
    const auto type_args = infer_expected_method_type_args(*symbols, receiver_type_ref, member.name,
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
    return method_template_callee(expr, receiver_type_ref, aliases, locals, local_type_refs,
                                  symbols, options) +
           "<" + lowered_args.str() + ">(" +
           join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                              options) +
           ")";
}

} // namespace dudu
