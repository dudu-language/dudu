#include "dudu/cpp_stmt_types.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_index_type_ref.hpp"
#include "dudu/sema_scan.hpp"

#include <cctype>
#include <cstddef>
#include <vector>

namespace dudu {
namespace {

std::string receiver_base_type_fallback(const TypeRef& type) {
    const std::string text = trim_copy(substitute_type_ref_text(type, {}));
    const size_t bracket = text.find('[');
    return bracket == std::string::npos ? text : trim_copy(text.substr(0, bracket));
}

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
        return type.children.empty() ? substitute_type_ref_text(type, {})
                                     : receiver_base_type(type.children.front());
    case TypeKind::Template:
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Function:
    case TypeKind::Value:
        return type_ref_head_name(type);
    case TypeKind::Unknown:
        break;
    }
    return receiver_base_type_fallback(type);
}

size_t index_count(const Expr& expr) {
    if (expr.kind == ExprKind::TupleLiteral && !expr.children.empty()) {
        return expr.children.size();
    }
    return 1;
}

TypeRef indexed_local_type_ref(const TypeRef& receiver_type, const Expr& index_expr) {
    const Symbols symbols;
    const auto indexed = indexed_type_ref_from_type_ref_with_count(
        symbols, index_expr.location, receiver_type, index_count(index_expr), false, false,
        substitute_type_ref_text(receiver_type, {}));
    return indexed ? *indexed : TypeRef{};
}

bool looks_like_dudu_type(const std::string& name) {
    return !name.empty() && name.find('.') == std::string::npos &&
           std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

TypeRef infer_call_type_ref(const std::string& callee,
                            const std::map<std::string, TypeRef>& function_returns,
                            const SourceLocation& location) {
    if (const auto fn = function_returns.find(callee); fn != function_returns.end()) {
        return fn->second;
    }
    if (looks_like_dudu_type(callee)) {
        return parse_type_text(callee, location);
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

TypeRef infer_call_type_ref(const Expr& expr, const std::map<std::string, std::string>& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const std::map<std::string, TypeRef>& function_returns,
                            const Symbols* symbols) {
    if (expr.callee.empty()) {
        return infer_call_type_ref(expr.name, function_returns, expr.location);
    }
    const Expr& callee = expr.callee.front();
    if (callee.kind == ExprKind::Name) {
        return infer_call_type_ref(callee.name, function_returns, callee.location);
    }
    if (callee.kind == ExprKind::Member && callee.children.size() == 1) {
        const TypeRef receiver_type = infer_emitted_local_type_ref(
            callee.children.front(), locals, local_type_refs, function_returns, symbols);
        if (has_type_ref(receiver_type)) {
            const std::string key = receiver_base_type(receiver_type) + "." + callee.name;
            if (const auto method = function_returns.find(key); method != function_returns.end()) {
                return method->second;
            }
        }
    }
    return {};
}

bool is_numeric_type(std::string_view type) {
    return type == "i32" || type == "f32" || type == "f64";
}

TypeRef infer_binary_expr_type_ref(const Expr& expr,
                                   const std::map<std::string, std::string>& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const std::map<std::string, TypeRef>& function_returns,
                                   const Symbols* symbols) {
    if (expr.children.size() != 2) {
        return {};
    }
    if (expr.op == "and" || expr.op == "or" || expr.op == "==" || expr.op == "!=" ||
        expr.op == "<" || expr.op == "<=" || expr.op == ">" || expr.op == ">=") {
        return parse_type_text("bool", expr.location);
    }
    const TypeRef left_ref = infer_emitted_local_type_ref(expr.children[0], locals, local_type_refs,
                                                          function_returns, symbols);
    const TypeRef right_ref = infer_emitted_local_type_ref(
        expr.children[1], locals, local_type_refs, function_returns, symbols);
    const std::string left = has_type_ref(left_ref) ? substitute_type_ref_text(left_ref, {}) : "";
    const std::string right =
        has_type_ref(right_ref) ? substitute_type_ref_text(right_ref, {}) : "";
    if (left.empty() && !right.empty()) {
        return right_ref;
    }
    if (right.empty() || right == left) {
        return left_ref;
    }
    if ((left == "f64" || right == "f64") && is_numeric_type(left) && is_numeric_type(right)) {
        return parse_type_text("f64", expr.location);
    }
    if ((left == "f32" || right == "f32") && (left == "f32" || left == "i32") &&
        (right == "f32" || right == "i32")) {
        return parse_type_text("f32", expr.location);
    }
    return {};
}

} // namespace

TypeRef infer_emitted_local_type_ref(const Expr& expr,
                                     const std::map<std::string, std::string>& locals,
                                     const std::map<std::string, TypeRef>& local_type_refs,
                                     const std::map<std::string, TypeRef>& function_returns,
                                     const Symbols* symbols) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
        return parse_type_text("bool", expr.location);
    case ExprKind::IntLiteral:
        return parse_type_text("i32", expr.location);
    case ExprKind::FloatLiteral:
        return parse_type_text("f64", expr.location);
    case ExprKind::StringLiteral:
        return parse_type_text("str", expr.location);
    case ExprKind::NoneLiteral:
        return parse_type_text("None", expr.location);
    case ExprKind::Name:
        return emitted_local_type_ref(local_type_refs, expr.name, expr.location);
    case ExprKind::Unary:
        if (expr.children.size() != 1) {
            return {};
        }
        if (expr.op == "not") {
            return parse_type_text("bool", expr.location);
        }
        if (expr.op == "&") {
            const TypeRef child = infer_emitted_local_type_ref(
                expr.children.front(), locals, local_type_refs, function_returns, symbols);
            if (!has_type_ref(child)) {
                return {};
            }
            TypeRef pointer;
            pointer.kind = TypeKind::Pointer;
            pointer.location = expr.location;
            pointer.children.push_back(child);
            pointer.text = substitute_type_ref_text(pointer, {});
            return pointer;
        }
        if (expr.op == "*") {
            TypeRef child = infer_emitted_local_type_ref(
                expr.children.front(), locals, local_type_refs, function_returns, symbols);
            if (child.kind == TypeKind::Pointer && child.children.size() == 1) {
                return child.children.front();
            }
            return {};
        }
        if (expr.op == "-") {
            return infer_emitted_local_type_ref(expr.children.front(), locals, local_type_refs,
                                                function_returns, symbols);
        }
        return {};
    case ExprKind::Call:
    case ExprKind::TemplateCall:
        return infer_call_type_ref(expr, locals, local_type_refs, function_returns, symbols);
    case ExprKind::Index:
        if (expr.children.size() == 2) {
            if (expr.children[0].kind == ExprKind::Name) {
                const TypeRef local_type = emitted_local_type_ref(
                    local_type_refs, expr.children[0].name, expr.children[0].location);
                if (const TypeRef indexed_type =
                        indexed_local_type_ref(local_type, expr.children[1]);
                    has_type_ref(indexed_type)) {
                    return indexed_type;
                }
            }
            const TypeRef receiver_type = infer_emitted_local_type_ref(
                expr.children[0], locals, local_type_refs, function_returns, symbols);
            if (has_type_ref(receiver_type)) {
                if (const TypeRef indexed_type =
                        indexed_local_type_ref(receiver_type, expr.children[1]);
                    has_type_ref(indexed_type)) {
                    return indexed_type;
                }
            }
        }
        return {};
    case ExprKind::Binary:
        return infer_binary_expr_type_ref(expr, locals, local_type_refs, function_returns, symbols);
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
    case ExprKind::Member:
    case ExprKind::NamedArg:
    case ExprKind::SetLiteral:
    case ExprKind::Slice:
    case ExprKind::TupleLiteral:
        return {};
    }
    return {};
}

} // namespace dudu
