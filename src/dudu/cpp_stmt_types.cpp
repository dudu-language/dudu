#include "dudu/cpp_stmt_types.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_signature_match.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_generics.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_native.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/type_compat.hpp"

#include <cstddef>
#include <vector>

namespace dudu {
namespace {

std::string receiver_base_type(const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Const:
    case TypeKind::Volatile:
    case TypeKind::Atomic:
    case TypeKind::Storage:
    case TypeKind::Shared:
    case TypeKind::Device:
    case TypeKind::Static:
    case TypeKind::FixedArray:
    case TypeKind::PackExpansion:
        return type.children.empty() ? std::string{} : receiver_base_type(type.children.front());
    case TypeKind::Template:
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Function:
    case TypeKind::Value:
    case TypeKind::Unknown:
        return type_ref_head_name(type);
    }
    return type_ref_head_name(type);
}

TypeRef indexed_local_type_ref(const TypeRef& receiver_type, const Expr& index_expr,
                               const Symbols* symbols) {
    const Symbols empty_symbols;
    const Symbols& active_symbols = symbols == nullptr ? empty_symbols : *symbols;
    const std::string label = type_ref_head_name(receiver_type).empty()
                                  ? std::string{"indexed value"}
                                  : type_ref_head_name(receiver_type);
    return indexed_type_ref_from_type(active_symbols, index_expr.location, receiver_type,
                                      index_expr, label);
}

TypeRef infer_call_type_ref(const std::string& callee,
                            const std::map<std::string, TypeRef>& function_returns,
                            const Symbols* symbols, const SourceLocation& location) {
    if (const auto fn = function_returns.find(callee); fn != function_returns.end()) {
        return fn->second;
    }
    if (symbols != nullptr &&
        (symbols->classes.contains(callee) || symbols->native_classes.contains(callee) ||
         symbols->types.contains(callee))) {
        return named_type_ref(callee, location);
    }
    return {};
}

TypeRef emitted_local_type_ref(const std::map<std::string, TypeRef>& local_type_refs,
                               const std::string& name, SourceLocation location) {
    if (const auto local = local_type_refs.find(name); local != local_type_refs.end()) {
        return local->second;
    }
    TypeRef unknown;
    unknown.location = location;
    return unknown;
}

TypeRef infer_call_type_ref(const Expr& expr, const std::map<std::string, TypeRef>& local_type_refs,
                            const std::map<std::string, TypeRef>& function_returns,
                            const Symbols* symbols) {
    if (expr.callee.empty()) {
        return infer_call_type_ref(expr.name, function_returns, symbols, expr.location);
    }
    const Expr& callee = expr.callee.front();
    if (callee.kind == ExprKind::Name) {
        return infer_call_type_ref(callee.name, function_returns, symbols, callee.location);
    }
    if (callee.kind == ExprKind::Member && callee.children.size() == 1) {
        const TypeRef receiver_type = infer_emitted_local_type_ref(
            callee.children.front(), local_type_refs, function_returns, symbols);
        if (has_type_ref(receiver_type)) {
            const std::string key = receiver_base_type(receiver_type) + "." + callee.name;
            if (const auto method = function_returns.find(key); method != function_returns.end()) {
                return method->second;
            }
        }
    }
    if (symbols != nullptr) {
        FunctionScope scope{*symbols};
        scope.local_type_refs = local_type_refs;
        if (const auto signature =
                match_native_signature(scope, direct_callee_name(expr), template_type_refs(expr),
                                       expr.children, nullptr)) {
            return signature_return_type_ref(*signature);
        }
    }
    return {};
}

bool is_numeric_type(const TypeRef& type) {
    return type_ref_is_name(type, "i32") || type_ref_is_name(type, "f32") ||
           type_ref_is_name(type, "f64");
}

TypeRef infer_binary_expr_type_ref(const Expr& expr,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const std::map<std::string, TypeRef>& function_returns,
                                   const Symbols* symbols) {
    if (expr.children.size() != 2) {
        return {};
    }
    if (expr.op == "and" || expr.op == "or" || expr.op == "==" || expr.op == "!=" ||
        expr.op == "<" || expr.op == "<=" || expr.op == ">" || expr.op == ">=") {
        return named_type_ref("bool", expr.location);
    }
    const TypeRef left_ref =
        infer_emitted_local_type_ref(expr.children[0], local_type_refs, function_returns, symbols);
    const TypeRef right_ref =
        infer_emitted_local_type_ref(expr.children[1], local_type_refs, function_returns, symbols);
    if (!has_type_ref(left_ref) && has_type_ref(right_ref)) {
        return right_ref;
    }
    if (!has_type_ref(right_ref) || type_ref_equivalent(left_ref, right_ref)) {
        return left_ref;
    }
    if ((type_ref_is_name(left_ref, "f64") || type_ref_is_name(right_ref, "f64")) &&
        is_numeric_type(left_ref) && is_numeric_type(right_ref)) {
        return named_type_ref("f64", expr.location);
    }
    if ((type_ref_is_name(left_ref, "f32") || type_ref_is_name(right_ref, "f32")) &&
        (type_ref_is_name(left_ref, "f32") || type_ref_is_name(left_ref, "i32")) &&
        (type_ref_is_name(right_ref, "f32") || type_ref_is_name(right_ref, "i32"))) {
        return named_type_ref("f32", expr.location);
    }
    return {};
}

} // namespace

TypeRef infer_emitted_local_type_ref(const Expr& expr,
                                     const std::map<std::string, TypeRef>& local_type_refs,
                                     const std::map<std::string, TypeRef>& function_returns,
                                     const Symbols* symbols) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
        return named_type_ref("bool", expr.location);
    case ExprKind::IntLiteral:
        return named_type_ref("i32", expr.location);
    case ExprKind::FloatLiteral:
        return named_type_ref("f64", expr.location);
    case ExprKind::StringLiteral:
        return named_type_ref("str", expr.location);
    case ExprKind::NoneLiteral:
        return named_type_ref("None", expr.location);
    case ExprKind::Name:
        return emitted_local_type_ref(local_type_refs, expr.name, expr.location);
    case ExprKind::Unary:
        if (expr.children.size() != 1) {
            return {};
        }
        if (expr.op == "not") {
            return named_type_ref("bool", expr.location);
        }
        if (expr.op == "&") {
            const TypeRef child = infer_emitted_local_type_ref(
                expr.children.front(), local_type_refs, function_returns, symbols);
            if (!has_type_ref(child)) {
                return {};
            }
            return wrapped_type_ref(TypeKind::Pointer, child, expr.location);
        }
        if (expr.op == "*") {
            TypeRef child = infer_emitted_local_type_ref(expr.children.front(), local_type_refs,
                                                         function_returns, symbols);
            if (child.kind == TypeKind::Pointer && child.children.size() == 1) {
                return child.children.front();
            }
            return {};
        }
        if (expr.op == "-") {
            return infer_emitted_local_type_ref(expr.children.front(), local_type_refs,
                                                function_returns, symbols);
        }
        return {};
    case ExprKind::Call:
    case ExprKind::TemplateCall:
        return infer_call_type_ref(expr, local_type_refs, function_returns, symbols);
    case ExprKind::Index:
        if (expr.children.size() == 2) {
            if (expr.children[0].kind == ExprKind::Name) {
                const TypeRef local_type = emitted_local_type_ref(
                    local_type_refs, expr.children[0].name, expr.children[0].location);
                if (const TypeRef indexed_type =
                        indexed_local_type_ref(local_type, expr.children[1], symbols);
                    has_type_ref(indexed_type)) {
                    return indexed_type;
                }
            }
            const TypeRef receiver_type = infer_emitted_local_type_ref(
                expr.children[0], local_type_refs, function_returns, symbols);
            if (has_type_ref(receiver_type)) {
                if (const TypeRef indexed_type =
                        indexed_local_type_ref(receiver_type, expr.children[1], symbols);
                    has_type_ref(indexed_type)) {
                    return indexed_type;
                }
            }
        }
        return {};
    case ExprKind::Binary:
        return infer_binary_expr_type_ref(expr, local_type_refs, function_returns, symbols);
    case ExprKind::Member:
        if (symbols != nullptr) {
            if (const auto native = native_member_expr_type_ref(*symbols, expr, expr.location)) {
                return *native;
            }
        }
        return {};
    case ExprKind::Missing:
    case ExprKind::Conditional:
    case ExprKind::Await:
    case ExprKind::Yield:
    case ExprKind::DefExpression:
    case ExprKind::Comprehension:
    case ExprKind::Unknown:
    case ExprKind::CppEscape:
    case ExprKind::DictEntry:
    case ExprKind::DictLiteral:
    case ExprKind::Lambda:
    case ExprKind::ListLiteral:
    case ExprKind::NamedArg:
    case ExprKind::SetLiteral:
    case ExprKind::Slice:
    case ExprKind::TupleLiteral:
        return {};
    }
    return {};
}

} // namespace dudu
