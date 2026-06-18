#include "dudu/sema_ops.hpp"

#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/decorators.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/type_compat.hpp"

#include <set>
#include <utility>
#include <vector>

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

TypeRef unwrap_value_type_ref(const Symbols& symbols, TypeRef type) {
    type = resolve_alias_ref(symbols, std::move(type));
    while (true) {
        type = resolve_alias_ref(symbols, std::move(type));
        if (const auto inner =
                unary_type_child_ref(type, {TypeKind::Const, TypeKind::Volatile, TypeKind::Storage,
                                            TypeKind::Shared, TypeKind::Device})) {
            type = *inner;
            continue;
        }
        return type;
    }
}

bool is_numeric_type(const TypeRef& type) {
    return is_numeric_type(type_ref_head_name(type));
}

const ClassDecl* dudu_class_for_type(const Symbols& symbols, const TypeRef& type) {
    const TypeRef value_type = unwrap_value_type_ref(symbols, type);
    const std::string name = type_ref_head_name(value_type);
    const auto klass = symbols.classes.find(name);
    return klass == symbols.classes.end() ? nullptr : klass->second;
}

bool unknown_or_auto(const TypeRef& type) {
    const std::string head = type_ref_head_name(type);
    return !has_type_ref(type) || type_ref_is_auto(type) || head == "reference" ||
           head == "const_reference" || head == "iterator" || head == "const_iterator" ||
           head.ends_with(".reference") || head.ends_with(".const_reference") ||
           head.ends_with(".iterator") || head.ends_with(".const_iterator");
}

bool same_foreign_cpp_type(const TypeRef& left, const TypeRef& right) {
    const std::string head = type_ref_head_name(left);
    return !head.empty() && head.find('.') != std::string::npos && type_ref_equivalent(left, right);
}

bool same_generic_param_type(const Symbols& symbols, const TypeRef& left, const TypeRef& right) {
    const std::string head = type_ref_head_name(left);
    return !head.empty() && type_ref_equivalent(left, right) &&
           symbols.generic_params.contains(head);
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
native_operator_signature(const Symbols& symbols, const std::string& op, const TypeRef& left,
                          const Expr* right_expr, const TypeRef& right) {
    const TypeRef value_left_ref = unwrap_value_type_ref(symbols, left);
    const TypeRef value_right_ref = unwrap_value_type_ref(symbols, right);
    const std::string value_left = substitute_type_ref_text(value_left_ref, {});
    for (const std::string& name : native_operator_names_for_type(value_left, op)) {
        const auto found = symbols.native_function_signatures.find(name);
        if (found == symbols.native_function_signatures.end()) {
            continue;
        }
        for (FunctionSignature signature : found->second) {
            if (signature_param_count(signature) < 2 ||
                !type_assignment_allowed(signature_param_type_ref(signature, 0), value_left_ref)) {
                continue;
            }
            if (right_expr != nullptr &&
                !assignment_type_allowed(signature_param_type_ref(signature, 1), *right_expr,
                                         value_right_ref)) {
                continue;
            }
            std::vector<TypeRef> remaining_params;
            remaining_params.reserve(signature_param_count(signature) - 1);
            for (size_t i = 1; i < signature_param_count(signature); ++i) {
                remaining_params.push_back(signature_param_type_ref(signature, i));
            }
            set_signature_param_types(signature, std::move(remaining_params));
            return signature;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<FunctionSignature>
dudu_operator_signature(const Symbols& symbols, const std::string& op, const TypeRef& left) {
    if (!is_supported_dudu_operator(op)) {
        return std::nullopt;
    }
    const ClassDecl* klass = dudu_class_for_type(symbols, left);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (!method_has_operator(method, op)) {
            continue;
        }
        FunctionSignature signature;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        std::vector<TypeRef> param_types;
        param_types.reserve(method.params.size() - first_param);
        for (size_t i = first_param; i < method.params.size(); ++i) {
            param_types.push_back(method.params[i].type_ref);
        }
        set_signature_param_types(signature, std::move(param_types));
        set_signature_return_type(signature, function_return_type_ref(method));
        return signature;
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
dudu_binary_operator_signature(const Symbols& symbols, const std::string& op, const TypeRef& left,
                               const Expr& right_expr, const TypeRef& right) {
    if (!is_supported_dudu_operator(op)) {
        return std::nullopt;
    }
    const ClassDecl* klass = dudu_class_for_type(symbols, left);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (!method_has_operator(method, op)) {
            continue;
        }
        FunctionSignature signature;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        std::vector<TypeRef> param_types;
        param_types.reserve(method.params.size() - first_param);
        for (size_t i = first_param; i < method.params.size(); ++i) {
            param_types.push_back(method.params[i].type_ref);
        }
        set_signature_param_types(signature, std::move(param_types));
        set_signature_return_type(signature, function_return_type_ref(method));
        if (signature_param_count(signature) == 0 ||
            assignment_type_allowed(signature_param_type_ref(signature, 0), right_expr, right)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
binary_operator_signature(const Symbols& symbols, const std::string& op, const TypeRef& left,
                          const Expr& right_expr, const TypeRef& right) {
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
    if (unknown_or_auto(left) || unknown_or_auto(right) || unknown_or_auto(value_left_ref) ||
        unknown_or_auto(value_right_ref)) {
        return true;
    }
    if (same_foreign_cpp_type(value_left_ref, value_right_ref)) {
        return true;
    }
    if (same_generic_param_type(symbols, value_left_ref, value_right_ref)) {
        return true;
    }
    if (op == "+" && type_ref_is_name(value_left_ref, "str")) {
        return assignment_type_allowed(value_left_ref, right_expr, value_right_ref);
    }
    if (op == "+" || op == "-") {
        return (resolved_left.kind == TypeKind::Pointer &&
                is_integer_type(type_ref_head_name(value_right_ref))) ||
               numeric_operand_allowed(value_left_ref, right_expr, value_right_ref);
    }
    if (op == "*" || op == "/") {
        return numeric_operand_allowed(value_left_ref, right_expr, value_right_ref);
    }
    if (op == "%" || op == "^" || op == "&" || op == "|" || op == "<<" || op == ">>") {
        return is_integer_type(type_ref_head_name(value_left_ref)) &&
               (assignment_type_allowed(value_left_ref, right_expr, value_right_ref) ||
                is_integer_type(type_ref_head_name(value_right_ref)));
    }
    return false;
}

bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const TypeRef& left,
                            const Expr& right_expr, const TypeRef& right) {
    const TypeRef value_left_ref = unwrap_value_type_ref(symbols, left);
    const TypeRef value_right_ref = unwrap_value_type_ref(symbols, right);
    if (unknown_or_auto(left) || unknown_or_auto(right) || unknown_or_auto(value_left_ref) ||
        unknown_or_auto(value_right_ref)) {
        return true;
    }
    if (same_foreign_cpp_type(value_left_ref, value_right_ref)) {
        return true;
    }
    if (same_generic_param_type(symbols, value_left_ref, value_right_ref)) {
        return true;
    }
    if (op == "==" || op == "!=") {
        return same_or_assignable(value_left_ref, right_expr, value_right_ref);
    }
    if (type_ref_is_name(value_left_ref, "str")) {
        return assignment_type_allowed(value_left_ref, right_expr, value_right_ref);
    }
    return numeric_operand_allowed(value_left_ref, right_expr, value_right_ref);
}

} // namespace dudu
