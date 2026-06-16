#include "dudu/sema_methods.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_builtin_methods.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_method_templates.hpp"
#include "dudu/sema_methods_internal.hpp"

#include <optional>

namespace dudu {
namespace {

bool method_is_static(const FunctionDecl& method) {
    return method.params.empty() || method.params.front().name != "self";
}

std::string template_method_name(const std::string& method_name) {
    const size_t open = method_name.find('[');
    return open == std::string::npos ? method_name : method_name.substr(0, open);
}

std::vector<std::string> template_method_args(const std::string& method_name) {
    const size_t open = method_name.find('[');
    if (open == std::string::npos || method_name.back() != ']') {
        return {};
    }
    return split_top_level_args(method_name.substr(open + 1, method_name.size() - open - 2));
}

} // namespace

FunctionSignature instantiate_method_signature(const ClassDecl& klass, const FunctionDecl& method,
                                               const std::vector<std::string>& receiver_args,
                                               const std::vector<std::string>& method_args) {
    FunctionSignature signature;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    for (size_t i = first_param; i < method.params.size(); ++i) {
        std::string param_type = substitute_method_template_type(
            method.params[i].type, method.generic_params, method_args);
        param_type = substitute_class_template_type(std::move(param_type), klass.generic_params,
                                                    receiver_args);
        signature.params.push_back(
            substitute_receiver_template_type(std::move(param_type), receiver_args));
    }
    signature.return_type = method.return_type.empty()
                                ? "void"
                                : substitute_method_template_type(
                                      method.return_type, method.generic_params, method_args);
    signature.return_type = substitute_class_template_type(std::move(signature.return_type),
                                                           klass.generic_params, receiver_args);
    signature.return_type =
        substitute_receiver_template_type(std::move(signature.return_type), receiver_args);
    return signature;
}

std::vector<std::string> type_ref_texts(const std::vector<TypeRef>& types) {
    std::vector<std::string> out;
    out.reserve(types.size());
    for (const TypeRef& type : types) {
        out.push_back(substitute_type_ref_text(type, {}));
    }
    return out;
}

bool method_signature_for_type(const Symbols& symbols, std::string receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location) {
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, signature)) {
        return true;
    }
    const std::string templated_receiver = receiver_template_type(symbols, receiver_type);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, std::move(receiver_type));
    const std::string lookup_name = template_method_name(method_name);
    const std::vector<std::string> method_args = template_method_args(method_name);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != lookup_name) {
            continue;
        }
        if (method.generic_params.size() != method_args.size()) {
            if (location != nullptr) {
                sema_fail(*location, "method " + type + "." + lookup_name + " expects " +
                                         std::to_string(method.generic_params.size()) +
                                         " type arguments, got " +
                                         std::to_string(method_args.size()));
            }
            return false;
        }
        signature =
            instantiate_method_signature(*klass->second, method, receiver_args, method_args);
        return true;
    }
    for (const std::string& base : klass->second->base_classes) {
        if (method_signature_for_type(symbols, base, method_name, signature, nullptr)) {
            return true;
        }
    }
    if (location != nullptr) {
        sema_fail(*location, "unknown method: " + type + "." + method_name);
    }
    return false;
}

std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, std::string receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location,
    const GenericInferCallbacks& callbacks) {
    const std::string templated_receiver = receiver_template_type(scope.symbols, receiver_type);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(scope.symbols, std::move(receiver_type));
    const auto klass = scope.symbols.classes.find(type);
    if (klass == scope.symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name || method.generic_params.empty()) {
            continue;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        const auto inferred = infer_generic_method_type_args(
            scope, method, type + "." + method_name, args, first_param, location, callbacks);
        if (!inferred) {
            return std::nullopt;
        }
        return instantiate_method_signature(*klass->second, method, receiver_args,
                                            type_ref_texts(*inferred));
    }
    for (const std::string& base : klass->second->base_classes) {
        if (const auto signature = inferred_generic_method_signature_for_type(
                scope, base, method_name, args, nullptr, callbacks)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, std::string receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const std::string& expected_return,
    const SourceLocation* location, const GenericInferCallbacks& callbacks) {
    const std::string templated_receiver = receiver_template_type(scope.symbols, receiver_type);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(scope.symbols, std::move(receiver_type));
    const auto klass = scope.symbols.classes.find(type);
    if (klass == scope.symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name || method.generic_params.empty()) {
            continue;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        std::vector<std::string> arg_types;
        arg_types.reserve(args.size());
        for (const Expr& arg : args) {
            arg_types.push_back(callbacks.infer_expr(scope, arg, location));
        }
        const auto inferred = infer_generic_method_type_args_from_types(
            method, type + "." + method_name, arg_types, first_param, expected_return, location);
        if (!inferred) {
            return std::nullopt;
        }
        return instantiate_method_signature(*klass->second, method, receiver_args,
                                            type_ref_texts(*inferred));
    }
    for (const std::string& base : klass->second->base_classes) {
        if (const auto signature = inferred_generic_method_signature_for_type(
                scope, base, method_name, args, expected_return, nullptr, callbacks)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          std::string receiver_type,
                                                          const std::string& method_name) {
    FunctionSignature builtin;
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, builtin)) {
        return {builtin};
    }
    std::vector<FunctionSignature> out;
    const std::string templated_receiver = receiver_template_type(symbols, receiver_type);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, std::move(receiver_type));
    const std::string lookup_name = template_method_name(method_name);
    const std::vector<std::string> method_args = template_method_args(method_name);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return out;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != lookup_name) {
            continue;
        }
        if (method.generic_params.size() != method_args.size()) {
            continue;
        }
        out.push_back(
            instantiate_method_signature(*klass->second, method, receiver_args, method_args));
    }
    for (const std::string& base : klass->second->base_classes) {
        std::vector<FunctionSignature> base_signatures =
            method_signatures_for_type(symbols, base, method_name);
        out.insert(out.end(), base_signatures.begin(), base_signatures.end());
    }
    return out;
}

bool static_method_signature_for_type(const Symbols& symbols, const std::string& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location) {
    const auto klass = symbols.classes.find(type_name);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name || !method_is_static(method)) {
            continue;
        }
        for (const ParamDecl& param : method.params) {
            signature.params.push_back(param.type);
        }
        signature.return_type = method.return_type.empty() ? "void" : method.return_type;
        return true;
    }
    if (location != nullptr) {
        sema_fail(*location, "unknown static method: " + type_name + "." + method_name);
    }
    return false;
}

} // namespace dudu
