#include "dudu/sema_methods.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_builtin_methods.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_method_templates.hpp"
#include "dudu/sema_methods_internal.hpp"

#include <map>
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

std::map<std::string, std::string> type_substitutions(const std::vector<std::string>& params,
                                                      const std::vector<std::string>& args) {
    std::map<std::string, std::string> out;
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        out.emplace(params[i], trim_copy(args[i]));
    }
    return out;
}

TypeRef instantiate_method_type_ref(const ClassDecl& klass, const FunctionDecl& method,
                                    const TypeRef& type,
                                    const std::vector<std::string>& receiver_args,
                                    const std::vector<std::string>& method_args) {
    TypeRef out = substitute_type_ref(type, type_substitutions(method.generic_params, method_args));
    out = substitute_type_ref(out, type_substitutions(klass.generic_params, receiver_args));
    return substitute_receiver_template_type(out, receiver_args);
}

} // namespace

FunctionSignature instantiate_method_signature(const ClassDecl& klass, const FunctionDecl& method,
                                               const std::vector<std::string>& receiver_args,
                                               const std::vector<std::string>& method_args) {
    FunctionSignature signature;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    for (size_t i = first_param; i < method.params.size(); ++i) {
        TypeRef param_type = instantiate_method_type_ref(klass, method, method.params[i].type_ref,
                                                         receiver_args, method_args);
        signature.params.push_back(substitute_type_ref_text(param_type, {}));
        signature.param_type_refs.push_back(std::move(param_type));
    }
    signature.return_type_ref =
        function_has_return_type(method)
            ? instantiate_method_type_ref(klass, method, method.return_type_ref, receiver_args,
                                          method_args)
            : void_type_ref(method.location);
    signature.return_type = substitute_type_ref_text(signature.return_type_ref, {});
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

bool method_signature_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location) {
    const std::string receiver_type_text = substitute_type_ref_text(receiver_type, {});
    if (builtin_cpp_method_signature(symbols, receiver_type_text, method_name, signature)) {
        return true;
    }
    const std::string templated_receiver = receiver_template_type(symbols, receiver_type_text);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, receiver_type);
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
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        if (method_signature_for_type(symbols, base_decl.type_ref, method_name, signature,
                                      nullptr)) {
            return true;
        }
    }
    if (location != nullptr) {
        sema_fail(*location, "unknown method: " + type + "." + method_name);
    }
    return false;
}

bool method_signature_for_type(const Symbols& symbols, std::string receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location) {
    return method_signature_for_type(symbols, parse_type_text(receiver_type), method_name,
                                     signature, location);
}

std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, const TypeRef& receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location,
    const GenericInferCallbacks& callbacks) {
    const std::string receiver_type_text = substitute_type_ref_text(receiver_type, {});
    const std::string templated_receiver =
        receiver_template_type(scope.symbols, receiver_type_text);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(scope.symbols, receiver_type);
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
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        if (const auto signature = inferred_generic_method_signature_for_type(
                scope, base_decl.type_ref, method_name, args, nullptr, callbacks)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::optional<FunctionSignature> inferred_generic_method_signature_for_type(
    const FunctionScope& scope, std::string receiver_type, const std::string& method_name,
    const std::vector<Expr>& args, const SourceLocation* location,
    const GenericInferCallbacks& callbacks) {
    return inferred_generic_method_signature_for_type(scope, parse_type_text(receiver_type),
                                                      method_name, args, location, callbacks);
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
        std::vector<TypeRef> arg_types;
        arg_types.reserve(args.size());
        for (const Expr& arg : args) {
            arg_types.push_back(callbacks.infer_expr_type(scope, arg, location));
        }
        const auto inferred = infer_generic_method_type_args_from_type_refs(
            method, type + "." + method_name, arg_types, first_param,
            expected_return.empty()
                ? std::nullopt
                : std::optional<TypeRef>{parse_type_text(expected_return, method.location)},
            location);
        if (!inferred) {
            return std::nullopt;
        }
        return instantiate_method_signature(*klass->second, method, receiver_args,
                                            type_ref_texts(*inferred));
    }
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        const std::string base = type_ref_text(base_decl.type_ref);
        if (const auto signature = inferred_generic_method_signature_for_type(
                scope, base, method_name, args, expected_return, nullptr, callbacks)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          const TypeRef& receiver_type,
                                                          const std::string& method_name) {
    const std::string receiver_type_text = substitute_type_ref_text(receiver_type, {});
    FunctionSignature builtin;
    if (builtin_cpp_method_signature(symbols, receiver_type_text, method_name, builtin)) {
        return {builtin};
    }
    std::vector<FunctionSignature> out;
    const std::string templated_receiver = receiver_template_type(symbols, receiver_type_text);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, receiver_type);
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
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        std::vector<FunctionSignature> base_signatures =
            method_signatures_for_type(symbols, base_decl.type_ref, method_name);
        out.insert(out.end(), base_signatures.begin(), base_signatures.end());
    }
    return out;
}

std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          std::string receiver_type,
                                                          const std::string& method_name) {
    return method_signatures_for_type(symbols, parse_type_text(receiver_type), method_name);
}

bool static_method_signature_for_type(const Symbols& symbols, const TypeRef& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location) {
    const std::string type_name_text = substitute_type_ref_text(type_name, {});
    const std::string templated_receiver = receiver_template_type(symbols, type_name_text);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, type_name);
    const std::string lookup_name = template_method_name(method_name);
    const std::vector<std::string> method_args = template_method_args(method_name);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != lookup_name || !method_is_static(method)) {
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
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        if (static_method_signature_for_type(symbols, base_decl.type_ref, method_name, signature,
                                             nullptr)) {
            return true;
        }
    }
    if (location != nullptr) {
        sema_fail(*location, "unknown static method: " + type + "." + method_name);
    }
    return false;
}

bool static_method_signature_for_type(const Symbols& symbols, const std::string& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location) {
    return static_method_signature_for_type(symbols, parse_type_text(type_name), method_name,
                                            signature, location);
}

} // namespace dudu
