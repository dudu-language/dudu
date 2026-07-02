#include "dudu/sema/sema_ops.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/type_compat.hpp"

#include <sstream>
#include <utility>
#include <vector>

namespace dudu {
namespace {

bool method_has_operator(const FunctionDecl& method, const std::string& op) {
    for (const Decorator& decorator : method.decorators) {
        if (decorator_first_string_arg(decorator, "operator") == op) {
            return true;
        }
    }
    return false;
}

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
    TypeRef out = substitute_generic_type_ref(method.generic_params, method_args, type);
    out = substitute_generic_type_ref(klass.generic_params, receiver_template_args(symbols, receiver),
                                      out);
    return substitute_type_ref(
        out, {{"Self", operator_self_type_ref(symbols, receiver, klass)}});
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

size_t method_first_argument_param(const FunctionDecl& method) {
    return !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
}

std::string operator_signature_label(const ClassDecl& klass, const FunctionDecl& method,
                                     const FunctionSignature& signature) {
    std::ostringstream out;
    out << klass.name << "." << method.name << "(";
    const size_t param_count = signature_param_count(signature);
    for (size_t i = 0; i < param_count; ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (signature.variadic && i == signature_variadic_param_index(signature)) {
            out << "*";
        }
        out << substitute_type_ref_text(signature_param_type_ref(signature, i), {});
    }
    out << ") -> " << substitute_type_ref_text(signature_return_type_ref(signature), {});
    return out.str();
}

bool pack_expansion_arg_matches(const Symbols& symbols, const FunctionSignature& signature,
                                size_t arg_index, const std::vector<Expr>& args,
                                const TypeRef& expected, const TypeRef& got) {
    if (args[arg_index].kind != ExprKind::PackExpansion || args[arg_index].children.size() != 1 ||
        got.kind != TypeKind::PackExpansion || got.children.size() != 1) {
        return false;
    }
    const size_t param_index = signature_param_index_for_arg(signature, arg_index, args.size());
    if (param_index != signature_variadic_param_index(signature)) {
        return false;
    }
    return assignment_type_allowed(resolve_alias_ref(symbols, expected),
                                   args[arg_index].children.front(),
                                   resolve_alias_ref(symbols, got.children.front()));
}

std::string operator_rejection_reason(const Symbols& symbols, const FunctionSignature& signature,
                                      const std::vector<Expr>& args,
                                      const std::vector<TypeRef>& arg_types) {
    const size_t param_count = signature_param_count(signature);
    const size_t min_arg_count = signature_min_arg_count(signature);
    if (args.size() != arg_types.size()) {
        return "internal arity mismatch while checking arguments";
    }
    if (!signature.variadic && param_count != args.size()) {
        return "expects " + std::to_string(param_count) + " arguments, got " +
               std::to_string(args.size());
    }
    if (signature.variadic && args.size() < min_arg_count) {
        return "expects at least " + std::to_string(min_arg_count) + " arguments, got " +
               std::to_string(args.size());
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (!has_type_ref(arg_types[i])) {
            return "argument " + std::to_string(i + 1) + " has unknown type";
        }
        const size_t param_index = signature_param_index_for_arg(signature, i, args.size());
        const TypeRef expected = signature_param_type_ref(signature, param_index);
        if (assignment_type_allowed(resolve_alias_ref(symbols, expected), args[i],
                                    resolve_alias_ref(symbols, arg_types[i])) ||
            pack_expansion_arg_matches(symbols, signature, i, args, expected, arg_types[i])) {
            continue;
        }
        return "argument " + std::to_string(i + 1) + " expects " +
               substitute_type_ref_text(expected, {}) + ", got " +
               substitute_type_ref_text(arg_types[i], {});
    }
    return "candidate did not match";
}

} // namespace

std::string dudu_operator_no_match_message_for_args(
    const Symbols& symbols, std::string_view op, const TypeRef& left,
    const std::vector<Expr>& args, const std::vector<TypeRef>& arg_types,
    std::string_view action, std::string_view label) {
    const std::string op_text(op);
    std::ostringstream out;
    out << "no matching @operator(\"" << op_text << "\") for " << action << " to " << label;
    const ClassDecl* klass = class_for_receiver_type(symbols, left);
    if (klass == nullptr) {
        return out.str();
    }
    bool saw_operator = false;
    for (const FunctionDecl& method : klass->methods) {
        if (!method_has_operator(method, op_text)) {
            continue;
        }
        saw_operator = true;
        std::vector<TypeRef> method_args;
        if (!method.generic_params.empty()) {
            const std::string display = klass->name + "." + method.name;
            const auto inferred = infer_generic_method_type_args_from_type_refs(
                method, display, arg_types, method_first_argument_param(method), std::nullopt,
                nullptr, &left);
            if (!inferred) {
                out << "\ncandidate " << klass->name << "." << method.name
                    << " rejected: cannot infer generic arguments";
                continue;
            }
            method_args = *inferred;
        }
        const FunctionSignature signature =
            operator_method_signature(symbols, left, *klass, method, method_args);
        out << "\ncandidate " << operator_signature_label(*klass, method, signature)
            << " rejected: " << operator_rejection_reason(symbols, signature, args, arg_types);
    }
    if (!saw_operator) {
        out << "\nreceiver type " << substitute_type_ref_text(left, {})
            << " declares no @operator(\"" << op_text << "\")";
    }
    return out.str();
}

} // namespace dudu
