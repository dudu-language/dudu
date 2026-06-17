#include "dudu/sema_ops.hpp"

#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/decorators.hpp"
#include "dudu/type_compat.hpp"

#include <set>

namespace dudu {
namespace {} // namespace

bool is_integer_type(std::string type) {
    type = trim(std::move(type));
    static const std::set<std::string> integers = {"i8",  "i16", "i32", "i64",   "u8",
                                                   "u16", "u32", "u64", "usize", "isize"};
    return integers.contains(type);
}

namespace {

bool is_numeric_type(std::string type) {
    type = trim(std::move(type));
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

std::string unwrap_value_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        type = trim(std::move(type));
        const TypeRef parsed = parse_type_text(type);
        if (const auto inner = unary_type_child_text(parsed, {TypeKind::Const, TypeKind::Volatile,
                                                              TypeKind::Storage, TypeKind::Shared,
                                                              TypeKind::Device})) {
            type = *inner;
            continue;
        }
        return type;
    }
}

bool unknown_or_auto(const std::string& type) {
    const std::string text = trim(type);
    return text.empty() || text == "auto" || text == "reference" || text == "const_reference" ||
           text == "iterator" || text == "const_iterator" || text.ends_with(".reference") ||
           text.ends_with(".const_reference") || text.ends_with(".iterator") ||
           text.ends_with(".const_iterator");
}

bool same_foreign_cpp_type(const std::string& left, const std::string& right) {
    const std::string lhs = trim(left);
    return !lhs.empty() && lhs == trim(right) && lhs.find('.') != std::string::npos;
}

bool same_generic_param_type(const Symbols& symbols, const std::string& left,
                             const std::string& right) {
    const std::string lhs = trim(left);
    return !lhs.empty() && lhs == trim(right) && symbols.generic_params.contains(lhs);
}

bool numeric_operand_allowed(const std::string& expected, const Expr& expr,
                             const std::string& got) {
    return is_numeric_type(expected) && assignment_type_allowed(expected, expr, got);
}

bool same_or_assignable(const std::string& left, const Expr& right_expr, const std::string& right) {
    return assignment_type_allowed(left, right_expr, right) || type_assignment_allowed(right, left);
}

bool is_supported_dudu_operator(const std::string& op) {
    static const std::set<std::string> operators = {
        "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "bool", "[]", "[]=",
    };
    return operators.contains(op);
}

bool method_has_operator(const FunctionDecl& method, const std::string& op) {
    for (const Decorator& decorator : method.decorators) {
        if (decorator_first_string_arg(decorator, "operator") == op) {
            return true;
        }
    }
    return false;
}

std::string operator_function_name(const std::string& op) {
    return "operator" + op;
}

std::vector<std::string> native_operator_names_for_type(const std::string& type,
                                                        const std::string& op) {
    std::vector<std::string> names;
    const std::string op_name = operator_function_name(op);
    const size_t dot = type.rfind('.');
    if (dot != std::string::npos) {
        names.push_back(type.substr(0, dot) + "." + op_name);
    }
    names.push_back(op_name);
    return names;
}

std::optional<FunctionSignature>
native_operator_signature(const Symbols& symbols, const std::string& op, const std::string& left,
                          const Expr* right_expr, const std::string& right) {
    const std::string value_left = unwrap_value_type(symbols, left);
    const std::string value_right = unwrap_value_type(symbols, right);
    for (const std::string& name : native_operator_names_for_type(value_left, op)) {
        const auto found = symbols.native_function_signatures.find(name);
        if (found == symbols.native_function_signatures.end()) {
            continue;
        }
        for (FunctionSignature signature : found->second) {
            if (signature.params.size() < 2 ||
                !type_assignment_allowed(signature.params.front(), value_left)) {
                continue;
            }
            if (right_expr != nullptr &&
                !assignment_type_allowed(signature.params[1], *right_expr, value_right)) {
                continue;
            }
            signature.params.erase(signature.params.begin());
            return signature;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<FunctionSignature>
dudu_operator_signature(const Symbols& symbols, const std::string& op, const std::string& left) {
    if (!is_supported_dudu_operator(op)) {
        return std::nullopt;
    }
    const auto klass = symbols.classes.find(unwrap_value_type(symbols, left));
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (!method_has_operator(method, op)) {
            continue;
        }
        FunctionSignature signature;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            signature.params.push_back(type_ref_text(method.params[i].type_ref));
        }
        signature.return_type = method.return_type.empty() ? "void" : method.return_type;
        return signature;
    }
    return std::nullopt;
}

std::optional<FunctionSignature> dudu_binary_operator_signature(const Symbols& symbols,
                                                                const std::string& op,
                                                                const std::string& left,
                                                                const Expr& right_expr,
                                                                const std::string& right) {
    if (!is_supported_dudu_operator(op)) {
        return std::nullopt;
    }
    const auto klass = symbols.classes.find(unwrap_value_type(symbols, left));
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (!method_has_operator(method, op)) {
            continue;
        }
        FunctionSignature signature;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            signature.params.push_back(type_ref_text(method.params[i].type_ref));
            signature.param_type_refs.push_back(method.params[i].type_ref);
        }
        signature.return_type = method.return_type.empty() ? "void" : method.return_type;
        signature.return_type_ref = method.return_type.empty()
                                        ? parse_type_text("void", method.location)
                                        : method.return_type_ref;
        if (signature.params.empty() ||
            assignment_type_allowed(signature.params.front(), right_expr, right)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
binary_operator_signature(const Symbols& symbols, const std::string& op, const std::string& left,
                          const Expr& right_expr, const std::string& right) {
    if (const auto signature =
            dudu_binary_operator_signature(symbols, op, left, right_expr, right)) {
        return signature;
    }
    return native_operator_signature(symbols, op, left, &right_expr, right);
}

bool binary_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                        const Expr& right_expr, const std::string& right) {
    const std::string resolved_left = resolve_alias(symbols, left);
    const std::string value_left = unwrap_value_type(symbols, left);
    const std::string value_right = unwrap_value_type(symbols, right);
    if (unknown_or_auto(left) || unknown_or_auto(right) || unknown_or_auto(value_left) ||
        unknown_or_auto(value_right)) {
        return true;
    }
    if (same_foreign_cpp_type(value_left, value_right)) {
        return true;
    }
    if (same_generic_param_type(symbols, value_left, value_right)) {
        return true;
    }
    if (op == "+" && value_left == "str") {
        return assignment_type_allowed(value_left, right_expr, value_right);
    }
    if (op == "+" || op == "-") {
        return (parse_type_text(resolved_left).kind == TypeKind::Pointer &&
                is_integer_type(value_right)) ||
               numeric_operand_allowed(value_left, right_expr, value_right);
    }
    if (op == "*" || op == "/") {
        return numeric_operand_allowed(value_left, right_expr, value_right);
    }
    if (op == "%" || op == "^" || op == "&" || op == "|" || op == "<<" || op == ">>") {
        return is_integer_type(value_left) &&
               (assignment_type_allowed(value_left, right_expr, value_right) ||
                is_integer_type(value_right));
    }
    return false;
}

bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                            const Expr& right_expr, const std::string& right) {
    const std::string value_left = unwrap_value_type(symbols, left);
    const std::string value_right = unwrap_value_type(symbols, right);
    if (unknown_or_auto(left) || unknown_or_auto(right) || unknown_or_auto(value_left) ||
        unknown_or_auto(value_right)) {
        return true;
    }
    if (same_foreign_cpp_type(value_left, value_right)) {
        return true;
    }
    if (same_generic_param_type(symbols, value_left, value_right)) {
        return true;
    }
    if (op == "==" || op == "!=") {
        return same_or_assignable(value_left, right_expr, value_right);
    }
    if (value_left == "str") {
        return assignment_type_allowed(value_left, right_expr, value_right);
    }
    return numeric_operand_allowed(value_left, right_expr, value_right);
}

} // namespace dudu
