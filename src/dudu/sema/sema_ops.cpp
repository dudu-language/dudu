#include "dudu/sema/sema_ops.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <map>
#include <set>
#include <utility>
#include <vector>

namespace dudu {
namespace {

TypeRef unwrap_value_type_ref(const Symbols& symbols, TypeRef type) {
    type = resolve_alias_ref(symbols, std::move(type));
    while (true) {
        type = resolve_alias_ref(symbols, std::move(type));
        if (const auto inner = unary_type_child_ref(type, {TypeKind::Reference, TypeKind::Const,
                                                           TypeKind::Volatile, TypeKind::Storage,
                                                           TypeKind::Shared, TypeKind::Device})) {
            type = *inner;
            continue;
        }
        return type;
    }
}

bool is_numeric_type(const TypeRef& type) {
    return type_ref_is_integer(type) || type_ref_is_name(type, "f32") ||
           type_ref_is_name(type, "f64");
}

bool unknown_or_auto(const TypeRef& type) {
    return !has_type_ref(type) || type_ref_is_auto(type) ||
           native_associated_operator_operand_is_dependent(type);
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
        "+",    "-",  "*",   "/",        "%",         "+=",       "-=",        "*=",
        "/=",   "%=", "==",  "!=",       "<",         "<=",       ">",         ">=",
        "bool", "[]", "[]=",
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

struct DuduOperatorCandidate {
    std::string method_name;
    FunctionSignature signature;
};

TypeRef operator_self_type_ref(const Symbols& symbols, const TypeRef& receiver,
                               const ClassDecl& klass) {
    TypeRef self = unwrap_value_type_ref(symbols, receiver);
    return has_type_ref(self) ? self : named_type_ref(klass.name, receiver.location);
}

TypeRef operator_method_type_ref(const Symbols& symbols, const TypeRef& receiver,
                                 const ClassDecl& klass, const TypeRef& type) {
    std::map<std::string, TypeRef> substitutions;
    substitutions.emplace("Self", operator_self_type_ref(symbols, receiver, klass));
    const std::vector<TypeRef> receiver_args = template_type_arg_refs(receiver, klass.name);
    for (size_t i = 0; i < klass.generic_params.size() && i < receiver_args.size(); ++i) {
        substitutions.emplace(klass.generic_params[i], receiver_args[i]);
    }
    return substitute_type_ref(type, substitutions);
}

FunctionSignature operator_method_signature(const Symbols& symbols, const TypeRef& receiver,
                                            const ClassDecl& klass, const FunctionDecl& method) {
    FunctionSignature signature;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    std::vector<TypeRef> param_types;
    param_types.reserve(method.params.size() - first_param);
    for (size_t i = first_param; i < method.params.size(); ++i) {
        param_types.push_back(
            operator_method_type_ref(symbols, receiver, klass, method.params[i].type_ref));
    }
    set_signature_param_types(signature, std::move(param_types));
    set_signature_return_type(
        signature,
        operator_method_type_ref(symbols, receiver, klass, function_return_type_ref(method)));
    return signature;
}

std::string operator_function_name(const std::string& op) {
    return "operator" + op;
}

std::vector<DuduOperatorCandidate>
dudu_operator_candidates(const Symbols& symbols, const std::string& op, const TypeRef& left) {
    std::vector<DuduOperatorCandidate> out;
    if (!is_supported_dudu_operator(op)) {
        return out;
    }
    const ClassDecl* klass = class_for_receiver_type(symbols, left);
    if (klass == nullptr) {
        return out;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (!method_has_operator(method, op)) {
            continue;
        }
        out.push_back(DuduOperatorCandidate{
            method.name, operator_method_signature(symbols, left, *klass, method)});
    }
    return out;
}

bool signature_matches_arg_types(const FunctionSignature& signature,
                                 const std::vector<TypeRef>& arg_types) {
    if (signature_param_count(signature) != arg_types.size()) {
        return false;
    }
    for (size_t i = 0; i < arg_types.size(); ++i) {
        if (!has_type_ref(arg_types[i])) {
            return false;
        }
        if (!type_assignment_allowed(signature_param_type_ref(signature, i), arg_types[i])) {
            return false;
        }
    }
    return true;
}

bool signature_matches_args(const FunctionSignature& signature, const std::vector<Expr>& args,
                            const std::vector<TypeRef>& arg_types) {
    if (signature_param_count(signature) != args.size() || args.size() != arg_types.size()) {
        return false;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (!has_type_ref(arg_types[i])) {
            return false;
        }
        if (!assignment_type_allowed(signature_param_type_ref(signature, i), args[i],
                                     arg_types[i])) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> native_operator_names_for_type(const TypeRef& type,
                                                        const std::string& op) {
    std::vector<std::string> names;
    const std::string op_name = operator_function_name(op);
    const std::string head = type_ref_head_name(type);
    const size_t cpp_namespace = head.rfind("::");
    if (cpp_namespace != std::string::npos) {
        names.push_back(head.substr(0, cpp_namespace) + "." + op_name);
    } else if (const size_t dot = head.rfind('.'); dot != std::string::npos) {
        names.push_back(head.substr(0, dot) + "." + op_name);
    }
    names.push_back(op_name);
    return names;
}

std::optional<FunctionSignature>
native_operator_signature(const Symbols& symbols, const std::string& op, const TypeRef& left,
                          const Expr* right_expr, const TypeRef& right) {
    const TypeRef value_left_ref = unwrap_value_type_ref(symbols, left);
    const std::string left_class = receiver_class_name(symbols, value_left_ref);
    if (symbols.classes.contains(left_class) && !symbols.native_classes.contains(left_class)) {
        return std::nullopt;
    }
    const TypeRef value_right_ref = unwrap_value_type_ref(symbols, right);
    for (const std::string& name : native_operator_names_for_type(value_left_ref, op)) {
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

std::optional<FunctionSignature> dudu_operator_signature(const Symbols& symbols,
                                                         std::string_view op, const TypeRef& left) {
    const std::string op_text(op);
    if (!is_supported_dudu_operator(op_text)) {
        return std::nullopt;
    }
    const ClassDecl* klass = class_for_receiver_type(symbols, left);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (!method_has_operator(method, op_text)) {
            continue;
        }
        return operator_method_signature(symbols, left, *klass, method);
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
dudu_operator_signature_for_args(const Symbols& symbols, std::string_view op, const TypeRef& left,
                                 const std::vector<Expr>& args,
                                 const std::vector<TypeRef>& arg_types) {
    const std::string op_text(op);
    for (const DuduOperatorCandidate& candidate :
         dudu_operator_candidates(symbols, op_text, left)) {
        if (signature_matches_args(candidate.signature, args, arg_types)) {
            return candidate.signature;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
dudu_operator_signature_for_arg_types(const Symbols& symbols, std::string_view op,
                                      const TypeRef& left, const std::vector<TypeRef>& arg_types) {
    const std::string op_text(op);
    for (const DuduOperatorCandidate& candidate :
         dudu_operator_candidates(symbols, op_text, left)) {
        if (signature_matches_arg_types(candidate.signature, arg_types)) {
            return candidate.signature;
        }
    }
    return std::nullopt;
}

std::optional<std::string>
dudu_operator_method_name_for_arg_types(const Symbols& symbols, std::string_view op,
                                        const TypeRef& left,
                                        const std::vector<TypeRef>& arg_types) {
    const std::string op_text(op);
    for (const DuduOperatorCandidate& candidate :
         dudu_operator_candidates(symbols, op_text, left)) {
        if (signature_matches_arg_types(candidate.signature, arg_types)) {
            return candidate.method_name;
        }
    }
    return std::nullopt;
}

std::optional<std::string>
dudu_operator_method_name_for_args(const Symbols& symbols, std::string_view op, const TypeRef& left,
                                   const std::vector<Expr>& args,
                                   const std::vector<TypeRef>& arg_types) {
    const std::string op_text(op);
    for (const DuduOperatorCandidate& candidate :
         dudu_operator_candidates(symbols, op_text, left)) {
        if (signature_matches_args(candidate.signature, args, arg_types)) {
            return candidate.method_name;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
dudu_binary_operator_signature(const Symbols& symbols, const std::string& op, const TypeRef& left,
                               const Expr& right_expr, const TypeRef& right) {
    if (!is_supported_dudu_operator(op)) {
        return std::nullopt;
    }
    const ClassDecl* klass = class_for_receiver_type(symbols, left);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (!method_has_operator(method, op)) {
            continue;
        }
        FunctionSignature signature = operator_method_signature(symbols, left, *klass, method);
        if (signature_param_count(signature) == 0 ||
            assignment_type_allowed(signature_param_type_ref(signature, 0), right_expr, right)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature> binary_operator_signature(const Symbols& symbols,
                                                           std::string_view op, const TypeRef& left,
                                                           const Expr& right_expr,
                                                           const TypeRef& right) {
    const std::string op_text(op);
    if (const auto signature =
            dudu_binary_operator_signature(symbols, op_text, left, right_expr, right)) {
        return signature;
    }
    return native_operator_signature(symbols, op_text, left, &right_expr, right);
}

bool binary_rhs_allowed(const Symbols& symbols, std::string_view op, const TypeRef& left,
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
        return (resolved_left.kind == TypeKind::Pointer && type_ref_is_integer(value_right_ref)) ||
               numeric_operand_allowed(value_left_ref, right_expr, value_right_ref);
    }
    if (op == "*" || op == "/") {
        return numeric_operand_allowed(value_left_ref, right_expr, value_right_ref);
    }
    if (op == "%" || op == "^" || op == "&" || op == "|" || op == "<<" || op == ">>") {
        return type_ref_is_integer(value_left_ref) &&
               (assignment_type_allowed(value_left_ref, right_expr, value_right_ref) ||
                type_ref_is_integer(value_right_ref));
    }
    return false;
}

bool comparison_rhs_allowed(const Symbols& symbols, std::string_view op, const TypeRef& left,
                            const Expr& right_expr, const TypeRef& right) {
    if ((op == "==" || op == "!=") && same_or_assignable(left, right_expr, right)) {
        return true;
    }
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
