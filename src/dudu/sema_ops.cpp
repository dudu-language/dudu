#include "dudu/sema_ops.hpp"

#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/decorators.hpp"
#include "dudu/sema_function_type.hpp"
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

bool is_numeric_type(const TypeRef& type) {
    return is_numeric_type(substitute_type_ref_text(type, {}));
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

TypeRef unwrap_value_type_ref(const Symbols& symbols, TypeRef type) {
    type = resolve_alias_ref(symbols, std::move(type));
    while (true) {
        type = resolve_alias_ref(symbols, std::move(type));
        if (const auto inner = unary_type_child_ref(type, {TypeKind::Const, TypeKind::Volatile,
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

bool numeric_operand_allowed(const TypeRef& expected, const Expr& expr, const TypeRef& got) {
    return is_numeric_type(expected) && assignment_type_allowed(expected, expr, got);
}

bool same_or_assignable(const TypeRef& left, const Expr& right_expr, const TypeRef& right) {
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
                !type_assignment_allowed(signature_param_type_ref(signature, 0),
                                         parse_type_text(value_left))) {
                continue;
            }
            if (right_expr != nullptr &&
                !assignment_type_allowed(signature_param_type_ref(signature, 1), *right_expr,
                                         value_right)) {
                continue;
            }
            signature.params.erase(signature.params.begin());
            if (!signature.param_type_refs.empty()) {
                signature.param_type_refs.erase(signature.param_type_refs.begin());
            }
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
            signature.param_type_refs.push_back(method.params[i].type_ref);
        }
        signature.return_type = function_return_type_text(method);
        signature.return_type_ref = function_return_type_ref(method);
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
        signature.return_type = function_return_type_text(method);
        signature.return_type_ref = function_return_type_ref(method);
        if (signature.params.empty() ||
            assignment_type_allowed(signature_param_type_ref(signature, 0), right_expr, right)) {
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

bool binary_rhs_allowed(const Symbols& symbols, const std::string& op, const TypeRef& left,
                        const Expr& right_expr, const TypeRef& right) {
    const TypeRef resolved_left = resolve_alias_ref(symbols, left);
    const TypeRef value_left_ref = unwrap_value_type_ref(symbols, left);
    const TypeRef value_right_ref = unwrap_value_type_ref(symbols, right);
    const std::string left_text = substitute_type_ref_text(left, {});
    const std::string right_text = substitute_type_ref_text(right, {});
    const std::string value_left = substitute_type_ref_text(value_left_ref, {});
    const std::string value_right = substitute_type_ref_text(value_right_ref, {});
    if (unknown_or_auto(left_text) || unknown_or_auto(right_text) || unknown_or_auto(value_left) ||
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
        return assignment_type_allowed(value_left_ref, right_expr, value_right_ref);
    }
    if (op == "+" || op == "-") {
        return (resolved_left.kind == TypeKind::Pointer &&
                is_integer_type(value_right)) ||
               numeric_operand_allowed(value_left_ref, right_expr, value_right_ref);
    }
    if (op == "*" || op == "/") {
        return numeric_operand_allowed(value_left_ref, right_expr, value_right_ref);
    }
    if (op == "%" || op == "^" || op == "&" || op == "|" || op == "<<" || op == ">>") {
        return is_integer_type(value_left) &&
               (assignment_type_allowed(value_left_ref, right_expr, value_right_ref) ||
                is_integer_type(value_right));
    }
    return false;
}

bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const TypeRef& left,
                            const Expr& right_expr, const TypeRef& right) {
    const TypeRef value_left_ref = unwrap_value_type_ref(symbols, left);
    const TypeRef value_right_ref = unwrap_value_type_ref(symbols, right);
    const std::string left_text = substitute_type_ref_text(left, {});
    const std::string right_text = substitute_type_ref_text(right, {});
    const std::string value_left = substitute_type_ref_text(value_left_ref, {});
    const std::string value_right = substitute_type_ref_text(value_right_ref, {});
    if (unknown_or_auto(left_text) || unknown_or_auto(right_text) || unknown_or_auto(value_left) ||
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
        return same_or_assignable(value_left_ref, right_expr, value_right_ref);
    }
    if (value_left == "str") {
        return assignment_type_allowed(value_left_ref, right_expr, value_right_ref);
    }
    return numeric_operand_allowed(value_left_ref, right_expr, value_right_ref);
}

} // namespace dudu
