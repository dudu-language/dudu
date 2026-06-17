#include "dudu/cpp_stmt_types.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_scan.hpp"

#include <cctype>
#include <cstddef>
#include <optional>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

std::string receiver_base_type(std::string type) {
    type = trim_copy(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    switch (parsed.kind) {
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
        return parsed.children.empty()
                   ? type
                   : receiver_base_type(substitute_type_ref_text(parsed.children.front(), {}));
    case TypeKind::Template:
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Function:
    case TypeKind::Value:
        return type_ref_head_name(parsed);
    case TypeKind::Unknown:
        break;
    }
    const size_t bracket = type.find('[');
    return bracket == std::string::npos ? type : trim_copy(type.substr(0, bracket));
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
    return receiver_base_type(substitute_type_ref_text(type, {}));
}

size_t index_count(const Expr& expr) {
    if (expr.kind == ExprKind::TupleLiteral && !expr.children.empty()) {
        return expr.children.size();
    }
    return 1;
}

std::string shaped_array_type(const std::string& element_type, const std::vector<size_t>& shape) {
    std::ostringstream out;
    out << "array[" << element_type << "][";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << shape[i];
    }
    out << "]";
    return out.str();
}

std::string indexed_local_type(const TypeRef& receiver_type, const Expr& index_expr);

std::string indexed_local_type(const TypeRef& receiver_type, const Expr& index_expr) {
    for (std::string_view name : {"list", "span", "strided_span", "set"}) {
        const std::vector<TypeRef> args = template_type_arg_refs(receiver_type, name);
        if (args.size() == 1) {
            return substitute_type_ref_text(args.front(), {});
        }
    }
    if (const std::vector<TypeRef> args = template_type_arg_refs(receiver_type, "dict");
        args.size() == 2) {
        return substitute_type_ref_text(args[1], {});
    }
    switch (receiver_type.kind) {
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Const:
    case TypeKind::Volatile:
    case TypeKind::Atomic:
    case TypeKind::Storage:
    case TypeKind::Shared:
    case TypeKind::Device:
    case TypeKind::Static:
        return receiver_type.children.empty()
                   ? std::string{}
                   : indexed_local_type(receiver_type.children.front(), index_expr);
    case TypeKind::Template:
    case TypeKind::FixedArray: {
        const std::string element_type = explicit_array_element_type(receiver_type);
        const std::vector<size_t> shape = explicit_array_shape(receiver_type);
        const size_t used_indices = index_count(index_expr);
        if (element_type.empty() || shape.empty() || used_indices >= shape.size()) {
            return element_type;
        }
        const std::vector<size_t> remaining_shape{
            shape.begin() + static_cast<std::ptrdiff_t>(used_indices), shape.end()};
        return shaped_array_type(element_type, remaining_shape);
    }
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Function:
    case TypeKind::Value:
    case TypeKind::Unknown:
        break;
    }
    return {};
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

TypeRef infer_call_type_ref(const Expr& expr, const std::map<std::string, std::string>& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const std::map<std::string, TypeRef>& function_returns) {
    if (expr.callee.empty()) {
        return infer_call_type_ref(expr.name, function_returns, expr.location);
    }
    const Expr& callee = expr.callee.front();
    if (callee.kind == ExprKind::Name) {
        return infer_call_type_ref(callee.name, function_returns, callee.location);
    }
    if (callee.kind == ExprKind::Member && callee.children.size() == 1) {
        const TypeRef receiver_type = infer_emitted_local_type_ref(
            callee.children.front(), locals, local_type_refs, function_returns);
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
                                   const std::map<std::string, TypeRef>& function_returns) {
    if (expr.children.size() != 2) {
        return {};
    }
    if (expr.op == "and" || expr.op == "or" || expr.op == "==" || expr.op == "!=" ||
        expr.op == "<" || expr.op == "<=" || expr.op == ">" || expr.op == ">=") {
        return parse_type_text("bool", expr.location);
    }
    const TypeRef left_ref =
        infer_emitted_local_type_ref(expr.children[0], locals, local_type_refs, function_returns);
    const TypeRef right_ref =
        infer_emitted_local_type_ref(expr.children[1], locals, local_type_refs, function_returns);
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
                                     const std::map<std::string, TypeRef>& function_returns) {
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
        if (const auto local = local_type_refs.find(expr.name); local != local_type_refs.end()) {
            return local->second;
        }
        if (const auto local = locals.find(expr.name); local != locals.end()) {
            return parse_type_text(local->second, expr.location);
        }
        return {};
    case ExprKind::Unary:
        if (expr.children.size() != 1) {
            return {};
        }
        if (expr.op == "not") {
            return parse_type_text("bool", expr.location);
        }
        if (expr.op == "&") {
            const TypeRef child = infer_emitted_local_type_ref(expr.children.front(), locals,
                                                               local_type_refs, function_returns);
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
            TypeRef child = infer_emitted_local_type_ref(expr.children.front(), locals,
                                                         local_type_refs, function_returns);
            if (child.kind == TypeKind::Pointer && child.children.size() == 1) {
                return child.children.front();
            }
            return {};
        }
        if (expr.op == "-") {
            return infer_emitted_local_type_ref(expr.children.front(), locals, local_type_refs,
                                                function_returns);
        }
        return {};
    case ExprKind::Call:
    case ExprKind::TemplateCall:
        return infer_call_type_ref(expr, locals, local_type_refs, function_returns);
    case ExprKind::Index:
        if (expr.children.size() == 2) {
            if (expr.children[0].kind == ExprKind::Name) {
                if (const auto local = local_type_refs.find(expr.children[0].name);
                    local != local_type_refs.end()) {
                    if (const std::string indexed_type =
                            indexed_local_type(local->second, expr.children[1]);
                        !indexed_type.empty()) {
                        return parse_type_text(indexed_type, expr.location);
                    }
                }
            }
            const TypeRef receiver_type = infer_emitted_local_type_ref(
                expr.children[0], locals, local_type_refs, function_returns);
            if (has_type_ref(receiver_type)) {
                if (const std::string indexed_type =
                        indexed_local_type(receiver_type, expr.children[1]);
                    !indexed_type.empty()) {
                    return parse_type_text(indexed_type, expr.location);
                }
            }
        }
        return {};
    case ExprKind::Binary:
        return infer_binary_expr_type_ref(expr, locals, local_type_refs, function_returns);
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
