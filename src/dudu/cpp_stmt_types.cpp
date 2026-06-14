#include "dudu/cpp_stmt_types.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
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
    while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
        type = trim_copy(type.substr(1));
    }
    for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
        const std::string prefix = std::string(wrapper) + "[";
        if (type.rfind(prefix, 0) == 0 && type.back() == ']') {
            return receiver_base_type(type.substr(prefix.size(), type.size() - prefix.size() - 1));
        }
    }
    const size_t bracket = type.find('[');
    return bracket == std::string::npos ? type : trim_copy(type.substr(0, bracket));
}

std::optional<std::vector<std::string>> template_args(std::string_view type,
                                                      std::string_view name) {
    const std::string prefix = std::string(name) + "[";
    if (!starts_with(type, prefix) || type.empty() || type.back() != ']') {
        return std::nullopt;
    }
    return split_top_level_args(
        std::string(type.substr(prefix.size(), type.size() - prefix.size() - 1)));
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

std::string indexed_local_type(const std::string& receiver_type, const Expr& index_expr) {
    const std::string type = trim_copy(receiver_type);
    if (const auto args = template_args(type, "list"); args && args->size() == 1) {
        return trim_copy(args->front());
    }
    if (const auto args = template_args(type, "span"); args && args->size() == 1) {
        return trim_copy(args->front());
    }
    if (const auto args = template_args(type, "set"); args && args->size() == 1) {
        return trim_copy(args->front());
    }
    if (const auto args = template_args(type, "dict"); args && args->size() == 2) {
        return trim_copy((*args)[1]);
    }
    if (starts_with(type, "array[")) {
        const std::string element_type = explicit_array_element_type(type);
        const std::vector<size_t> shape = explicit_array_shape(type);
        const size_t used_indices = index_count(index_expr);
        if (element_type.empty() || shape.empty() || used_indices >= shape.size()) {
            return element_type;
        }
        const std::vector<size_t> remaining_shape{
            shape.begin() + static_cast<std::ptrdiff_t>(used_indices), shape.end()};
        return shaped_array_type(element_type, remaining_shape);
    }
    return {};
}

bool looks_like_dudu_type(const std::string& name) {
    return !name.empty() && name.find('.') == std::string::npos &&
           std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

std::string infer_call_type(const std::string& callee,
                            const std::map<std::string, std::string>& locals,
                            const std::map<std::string, std::string>& function_returns) {
    if (const auto fn = function_returns.find(callee); fn != function_returns.end()) {
        return fn->second;
    }
    if (looks_like_dudu_type(callee)) {
        return callee;
    }
    const size_t dot = callee.rfind('.');
    if (dot != std::string::npos) {
        const std::string receiver = trim_copy(callee.substr(0, dot));
        const auto local = locals.find(receiver);
        if (local != locals.end()) {
            const std::string method_name = trim_copy(callee.substr(dot + 1));
            const std::string receiver_type = trim_copy(local->second);
            const std::string key = receiver_base_type(receiver_type) + "." + method_name;
            if (const auto method = function_returns.find(key); method != function_returns.end()) {
                return method->second;
            }
        }
    }
    return {};
}

std::string infer_binary_expr_type(const Expr& expr,
                                   const std::map<std::string, std::string>& locals,
                                   const std::map<std::string, std::string>& function_returns) {
    if (expr.children.size() != 2) {
        return {};
    }
    if (expr.op == "and" || expr.op == "or" || expr.op == "==" || expr.op == "!=" ||
        expr.op == "<" || expr.op == "<=" || expr.op == ">" || expr.op == ">=") {
        return "bool";
    }
    const std::string left = infer_emitted_local_type(expr.children[0], locals, function_returns);
    const std::string right = infer_emitted_local_type(expr.children[1], locals, function_returns);
    if (left.empty()) {
        return right;
    }
    if (right.empty() || right == left) {
        return left;
    }
    if ((left == "f64" || right == "f64") && (left == "f64" || left == "f32" || left == "i32") &&
        (right == "f64" || right == "f32" || right == "i32")) {
        return "f64";
    }
    if ((left == "f32" || right == "f32") && (left == "f32" || left == "i32") &&
        (right == "f32" || right == "i32")) {
        return "f32";
    }
    return {};
}

} // namespace

std::string infer_emitted_local_type(const std::string& expr,
                                     const std::map<std::string, std::string>& locals,
                                     const std::map<std::string, std::string>& function_returns) {
    const std::string text = trim_copy(expr);
    if (const auto local = locals.find(text); local != locals.end()) {
        return local->second;
    }
    const size_t call = find_call_open(text);
    if (call != std::string::npos && find_call_close(text, call) == text.size() - 1) {
        const std::string callee = trim_copy(text.substr(0, call));
        return infer_call_type(callee, locals, function_returns);
    }
    return {};
}

std::string infer_emitted_local_type(const Expr& expr,
                                     const std::map<std::string, std::string>& locals,
                                     const std::map<std::string, std::string>& function_returns) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
        return "bool";
    case ExprKind::IntLiteral:
        return "i32";
    case ExprKind::FloatLiteral:
        return "f64";
    case ExprKind::StringLiteral:
        return "str";
    case ExprKind::NoneLiteral:
        return "None";
    case ExprKind::Name:
        if (const auto local = locals.find(expr.name); local != locals.end()) {
            return local->second;
        }
        return {};
    case ExprKind::Unary:
        if (expr.children.size() != 1) {
            return {};
        }
        if (expr.op == "not") {
            return "bool";
        }
        if (expr.op == "&") {
            const std::string child =
                infer_emitted_local_type(expr.children.front(), locals, function_returns);
            return child.empty() ? std::string{} : "*" + child;
        }
        if (expr.op == "*") {
            std::string child = trim_copy(
                infer_emitted_local_type(expr.children.front(), locals, function_returns));
            if (!child.empty() && child.front() == '*') {
                return trim_copy(child.substr(1));
            }
            return {};
        }
        if (expr.op == "-") {
            return infer_emitted_local_type(expr.children.front(), locals, function_returns);
        }
        return {};
    case ExprKind::Binary:
        return infer_binary_expr_type(expr, locals, function_returns);
    case ExprKind::Conditional:
        if (expr.children.size() == 3) {
            const std::string then_type =
                infer_emitted_local_type(expr.children[0], locals, function_returns);
            const std::string else_type =
                infer_emitted_local_type(expr.children[2], locals, function_returns);
            return then_type == else_type ? then_type : std::string{};
        }
        return {};
    case ExprKind::Await:
        return {};
    case ExprKind::Call:
        return infer_call_type(call_callee_text(expr), locals, function_returns);
    case ExprKind::Index:
        if (expr.children.size() == 2 && expr.children[0].kind == ExprKind::Name) {
            if (const auto local = locals.find(expr.children[0].name); local != locals.end()) {
                if (const std::string indexed_type =
                        indexed_local_type(local->second, expr.children[1]);
                    !indexed_type.empty()) {
                    return indexed_type;
                }
            }
        }
        return {};
    case ExprKind::TemplateCall:
        return infer_call_type(call_callee_text(expr), locals, function_returns);
    case ExprKind::Unknown:
        return infer_emitted_local_type(expr.text, locals, function_returns);
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
