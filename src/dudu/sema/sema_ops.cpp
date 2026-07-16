#include "dudu/sema/sema_ops.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
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
    return native_numeric_operator_operand(type);
}

bool unknown_or_auto(const TypeRef& type) {
    return !has_type_ref(type) || type_ref_is_auto(type) ||
           native_associated_operator_operand_is_dependent(type);
}

bool same_foreign_cpp_type(const TypeRef& left, const TypeRef& right) {
    const std::string spelling = type_ref_text(left);
    return !spelling.empty() && spelling.find('.') != std::string::npos &&
           (type_ref_equivalent(left, right) || spelling == type_ref_text(right));
}

bool same_generic_param_type(const Symbols& symbols, const TypeRef& left, const TypeRef& right) {
    const std::string head = type_ref_head_name(left);
    return !head.empty() && type_ref_equivalent(left, right) &&
           symbols.generic_params.contains(head);
}

bool numeric_operand_allowed(const Symbols& symbols, const TypeRef& expected, const Expr& expr,
                             const TypeRef& got) {
    return is_numeric_type(expected) && assignment_type_allowed(symbols, expected, expr, got);
}

bool same_or_assignable(const Symbols& symbols, const TypeRef& left, const Expr& right_expr,
                        const TypeRef& right) {
    return assignment_type_allowed(symbols, left, right_expr, right) ||
           type_assignment_allowed(symbols, right, left);
}

bool pack_expansion_type_matches(const Symbols& symbols, const FunctionSignature& signature,
                                 size_t arg_index, size_t arg_count, const TypeRef& expected,
                                 const TypeRef& got) {
    if (!signature.variadic || got.kind != TypeKind::PackExpansion || got.children.size() != 1) {
        return false;
    }
    const size_t param_index = signature_param_index_for_arg(signature, arg_index, arg_count);
    if (param_index != signature_variadic_param_index(signature)) {
        return false;
    }
    return type_assignment_allowed(symbols, expected, got.children.front());
}

bool pack_expansion_arg_matches(const Symbols& symbols, const FunctionSignature& signature,
                                size_t arg_index, const std::vector<Expr>& args,
                                const TypeRef& expected, const TypeRef& got) {
    if (args[arg_index].kind != ExprKind::PackExpansion || args[arg_index].children.size() != 1) {
        return false;
    }
    if (!pack_expansion_type_matches(symbols, signature, arg_index, args.size(), expected, got)) {
        return false;
    }
    return assignment_type_allowed(symbols, expected, args[arg_index].children.front(),
                                   got.children.front());
}

bool is_supported_dudu_operator(const std::string& op) {
    static const std::set<std::string> operators = {
        "+",  "-",  "*", "/",  "%", "+=", "-=",   "*=", "/=",  "%=",
        "==", "!=", "<", "<=", ">", ">=", "bool", "[]", "[]=", "()",
    };
    return operators.contains(op);
}

bool method_has_operator(const FunctionDecl& method, const std::string& op) {
    for (const Decorator& decorator : method.decorators) {
        if (decorator_first_string_literal_arg(decorator, "operator") == op) {
            return true;
        }
    }
    return false;
}

TypeRef operator_self_type_ref(const Symbols& symbols, const TypeRef& receiver,
                               const ClassDecl& klass) {
    TypeRef self = unwrap_value_type_ref(symbols, receiver);
    return has_type_ref(self) ? self : named_type_ref(klass.name, receiver.location);
}

std::vector<TypeRef> receiver_template_args(const Symbols& symbols, TypeRef receiver) {
    receiver = resolve_alias_ref(symbols, std::move(receiver));
    while (true) {
        if (receiver.kind == TypeKind::Template) {
            return receiver.children;
        }
        if (!receiver.children.empty() &&
            (receiver.kind == TypeKind::Pointer || receiver.kind == TypeKind::Reference ||
             receiver.kind == TypeKind::Const || receiver.kind == TypeKind::Volatile ||
             receiver.kind == TypeKind::Atomic || receiver.kind == TypeKind::Storage ||
             receiver.kind == TypeKind::Shared || receiver.kind == TypeKind::Device ||
             receiver.kind == TypeKind::Shaped)) {
            receiver = resolve_alias_ref(symbols, receiver.children.front());
            continue;
        }
        return {};
    }
}

TypeRef operator_method_type_ref(const Symbols& symbols, const TypeRef& receiver,
                                 const ClassDecl& klass, const FunctionDecl& method,
                                 const std::vector<TypeRef>& method_args, const TypeRef& type) {
    std::map<std::string, TypeRef> substitutions;
    substitutions.emplace("Self", operator_self_type_ref(symbols, receiver, klass));
    const std::vector<TypeRef> receiver_args = receiver_template_args(symbols, receiver);
    TypeRef out = substitute_generic_type_ref(method.generic_params, method_args, type);
    out = substitute_generic_type_ref(klass.generic_params, receiver_args, out);
    return substitute_type_ref(out, substitutions);
}

FunctionSignature operator_method_signature(const Symbols& symbols, const TypeRef& receiver,
                                            const ClassDecl& klass, const FunctionDecl& method,
                                            const std::vector<TypeRef>& method_args = {}) {
    FunctionSignature signature;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    std::vector<TypeRef> param_types;
    param_types.reserve(method.params.size() - first_param);
    for (size_t i = first_param; i < method.params.size(); ++i) {
        TypeRef param_type = operator_method_type_ref(symbols, receiver, klass, method, method_args,
                                                      method.params[i].type_ref);
        if (method.params[i].variadic &&
            generic_pack_param_named(method.generic_params,
                                     type_ref_head_name(method.params[i].type_ref))) {
            param_type = named_type_ref("auto", method.params[i].location);
        }
        param_types.push_back(std::move(param_type));
    }
    set_signature_param_types(signature, std::move(param_types));
    for (size_t i = first_param; i < method.params.size(); ++i) {
        if (method.params[i].variadic) {
            signature.variadic = true;
            signature.variadic_param_index = static_cast<int>(i - first_param);
            break;
        }
    }
    set_signature_return_type(
        signature, operator_method_type_ref(symbols, receiver, klass, method, method_args,
                                            function_return_type_ref(method)));
    return signature;
}

std::string operator_function_name(const std::string& op) {
    return "operator" + op;
}

size_t method_first_argument_param(const FunctionDecl& method) {
    return !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
}

std::optional<DuduOperatorInstantiation> dudu_operator_candidate_for_arg_types(
    const Symbols& symbols, const std::string& op, const TypeRef& left, const ClassDecl& klass,
    const FunctionDecl& method, const std::vector<TypeRef>& arg_types) {
    if (!method_has_operator(method, op)) {
        return std::nullopt;
    }
    std::vector<TypeRef> method_args;
    if (!method.generic_params.empty()) {
        const std::string display = klass.name + "." + method.name;
        const auto inferred = infer_generic_method_type_args_from_type_refs(
            method, display, arg_types, method_first_argument_param(method), std::nullopt, nullptr,
            &left);
        if (!inferred) {
            return std::nullopt;
        }
        method_args = *inferred;
    }
    FunctionSignature signature =
        operator_method_signature(symbols, left, klass, method, method_args);
    return DuduOperatorInstantiation{
        .owner = &klass,
        .method = &method,
        .receiver_type = operator_self_type_ref(symbols, left, klass),
        .receiver_args = receiver_template_args(symbols, left),
        .method_args = std::move(method_args),
        .signature = std::move(signature),
    };
}

bool signature_matches_arg_types(const Symbols& symbols, const FunctionSignature& signature,
                                 const std::vector<TypeRef>& arg_types) {
    const size_t param_count = signature_param_count(signature);
    const size_t min_arg_count = signature_min_arg_count(signature);
    if ((!signature.variadic && param_count != arg_types.size()) ||
        (signature.variadic && arg_types.size() < min_arg_count)) {
        return false;
    }
    for (size_t i = 0; i < arg_types.size(); ++i) {
        if (!has_type_ref(arg_types[i])) {
            return false;
        }
        const TypeRef expected = signature_param_type_ref(
            signature, signature_param_index_for_arg(signature, i, arg_types.size()));
        if (!type_assignment_allowed(symbols, expected, arg_types[i]) &&
            !pack_expansion_type_matches(symbols, signature, i, arg_types.size(), expected,
                                         arg_types[i])) {
            return false;
        }
    }
    return true;
}

bool signature_matches_args(const Symbols& symbols, const FunctionSignature& signature,
                            const std::vector<Expr>& args, const std::vector<TypeRef>& arg_types) {
    const size_t param_count = signature_param_count(signature);
    const size_t min_arg_count = signature_min_arg_count(signature);
    if (((!signature.variadic && param_count != args.size()) ||
         (signature.variadic && args.size() < min_arg_count)) ||
        args.size() != arg_types.size()) {
        return false;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (!has_type_ref(arg_types[i])) {
            return false;
        }
        const TypeRef expected = signature_param_type_ref(
            signature, signature_param_index_for_arg(signature, i, args.size()));
        if (!assignment_type_allowed(symbols, expected, args[i], arg_types[i]) &&
            !pack_expansion_arg_matches(symbols, signature, i, args, expected, arg_types[i])) {
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
    if (symbols.classes.contains(left_class) && !is_native_class_binding(symbols, left_class)) {
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
                !type_assignment_allowed(symbols, signature_param_type_ref(signature, 0),
                                         value_left_ref)) {
                continue;
            }
            if (right_expr != nullptr &&
                !assignment_type_allowed(symbols, signature_param_type_ref(signature, 1),
                                         *right_expr, value_right_ref)) {
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
    const auto candidate = dudu_operator_instantiation_for_args(symbols, op, left, args, arg_types);
    return candidate ? std::optional<FunctionSignature>{candidate->signature} : std::nullopt;
}

std::optional<DuduOperatorInstantiation>
dudu_operator_instantiation_for_args(const Symbols& symbols, std::string_view op,
                                     const TypeRef& left, const std::vector<Expr>& args,
                                     const std::vector<TypeRef>& arg_types) {
    const std::string op_text(op);
    const ClassDecl* klass = class_for_receiver_type(symbols, left);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        const auto candidate = dudu_operator_candidate_for_arg_types(symbols, op_text, left, *klass,
                                                                     method, arg_types);
        if (!candidate) {
            continue;
        }
        if (signature_matches_args(symbols, candidate->signature, args, arg_types)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature>
dudu_operator_signature_for_arg_types(const Symbols& symbols, std::string_view op,
                                      const TypeRef& left, const std::vector<TypeRef>& arg_types) {
    const std::string op_text(op);
    const ClassDecl* klass = class_for_receiver_type(symbols, left);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        const auto candidate = dudu_operator_candidate_for_arg_types(symbols, op_text, left, *klass,
                                                                     method, arg_types);
        if (!candidate) {
            continue;
        }
        if (signature_matches_arg_types(symbols, candidate->signature, arg_types)) {
            return candidate->signature;
        }
    }
    return std::nullopt;
}

std::optional<std::string>
dudu_operator_method_name_for_arg_types(const Symbols& symbols, std::string_view op,
                                        const TypeRef& left,
                                        const std::vector<TypeRef>& arg_types) {
    const std::string op_text(op);
    const ClassDecl* klass = class_for_receiver_type(symbols, left);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        const auto candidate = dudu_operator_candidate_for_arg_types(symbols, op_text, left, *klass,
                                                                     method, arg_types);
        if (!candidate) {
            continue;
        }
        if (signature_matches_arg_types(symbols, candidate->signature, arg_types)) {
            return candidate->method->name;
        }
    }
    return std::nullopt;
}

std::optional<std::string>
dudu_operator_method_name_for_args(const Symbols& symbols, std::string_view op, const TypeRef& left,
                                   const std::vector<Expr>& args,
                                   const std::vector<TypeRef>& arg_types) {
    const std::string op_text(op);
    const ClassDecl* klass = class_for_receiver_type(symbols, left);
    if (klass == nullptr) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->methods) {
        const auto candidate = dudu_operator_candidate_for_arg_types(symbols, op_text, left, *klass,
                                                                     method, arg_types);
        if (!candidate) {
            continue;
        }
        if (signature_matches_args(symbols, candidate->signature, args, arg_types)) {
            return candidate->method->name;
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
        const auto candidate =
            dudu_operator_candidate_for_arg_types(symbols, op, left, *klass, method, {right});
        if (!candidate) {
            continue;
        }
        if (signature_matches_args(symbols, candidate->signature, {right_expr}, {right})) {
            return candidate->signature;
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

void check_instantiated_dudu_operator_body(const FunctionScope& scope, std::string_view op,
                                           const TypeRef& receiver_type,
                                           const std::vector<Expr>& args,
                                           const std::vector<TypeRef>& arg_types,
                                           const SourceLocation& instantiation_site) {
    const auto candidate =
        dudu_operator_instantiation_for_args(scope.symbols, op, receiver_type, args, arg_types);
    if (!candidate || candidate->owner == nullptr || candidate->method == nullptr) {
        return;
    }
    check_instantiated_generic_method_body(scope, *candidate->owner, *candidate->method,
                                           candidate->receiver_type, candidate->receiver_args,
                                           candidate->method_args, instantiation_site);
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
        return assignment_type_allowed(symbols, value_left_ref, right_expr, value_right_ref);
    }
    if (op == "+" || op == "-") {
        return (resolved_left.kind == TypeKind::Pointer && type_ref_is_integer(value_right_ref)) ||
               numeric_operand_allowed(symbols, value_left_ref, right_expr, value_right_ref);
    }
    if (op == "*" || op == "/") {
        return numeric_operand_allowed(symbols, value_left_ref, right_expr, value_right_ref);
    }
    if (op == "%" || op == "^" || op == "&" || op == "|" || op == "<<" || op == ">>") {
        return type_ref_is_integer(value_left_ref) &&
               (assignment_type_allowed(symbols, value_left_ref, right_expr, value_right_ref) ||
                type_ref_is_integer(value_right_ref));
    }
    return false;
}

bool comparison_rhs_allowed(const Symbols& symbols, std::string_view op, const TypeRef& left,
                            const Expr& right_expr, const TypeRef& right) {
    if ((op == "==" || op == "!=") && same_or_assignable(symbols, left, right_expr, right)) {
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
        return same_or_assignable(symbols, value_left_ref, right_expr, value_right_ref);
    }
    if (type_ref_is_name(value_left_ref, "str")) {
        return assignment_type_allowed(symbols, value_left_ref, right_expr, value_right_ref);
    }
    return numeric_operand_allowed(symbols, value_left_ref, right_expr, value_right_ref);
}

} // namespace dudu
