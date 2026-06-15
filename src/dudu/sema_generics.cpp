#include "dudu/sema_generics.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_common.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

namespace dudu {

namespace {

std::optional<size_t> generic_param_index(const std::vector<std::string>& params,
                                          const std::string& name) {
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == name) {
            return i;
        }
    }
    return std::nullopt;
}

std::string join_substituted_types(const std::vector<TypeRef>& types, size_t start,
                                   const std::vector<std::string>& params,
                                   const std::vector<TypeRef>& args);

std::string substitute_generic_type_ref(const TypeRef& type, const std::vector<std::string>& params,
                                        const std::vector<TypeRef>& args) {
    const std::string name = trim(type.name.empty() ? type.text : type.name);
    if (const auto index = generic_param_index(params, name); index && *index < args.size()) {
        return trim(args[*index].text);
    }

    switch (type.kind) {
    case TypeKind::Pointer:
        return type.children.empty()
                   ? trim(type.text)
                   : "*" + substitute_generic_type_ref(type.children[0], params, args);
    case TypeKind::Reference:
        return type.children.empty()
                   ? trim(type.text)
                   : "&" + substitute_generic_type_ref(type.children[0], params, args);
    case TypeKind::Const:
        return type.children.empty()
                   ? trim(type.text)
                   : "const[" + substitute_generic_type_ref(type.children[0], params, args) + "]";
    case TypeKind::Volatile:
        return type.children.empty()
                   ? trim(type.text)
                   : "volatile[" + substitute_generic_type_ref(type.children[0], params, args) +
                         "]";
    case TypeKind::Atomic:
        return type.children.empty()
                   ? trim(type.text)
                   : "atomic[" + substitute_generic_type_ref(type.children[0], params, args) + "]";
    case TypeKind::Device:
        return type.children.empty()
                   ? trim(type.text)
                   : "device[" + substitute_generic_type_ref(type.children[0], params, args) + "]";
    case TypeKind::Storage:
        return type.children.empty()
                   ? trim(type.text)
                   : "storage[" + substitute_generic_type_ref(type.children[0], params, args) + "]";
    case TypeKind::Shared:
        return type.children.empty()
                   ? trim(type.text)
                   : "shared[" + substitute_generic_type_ref(type.children[0], params, args) + "]";
    case TypeKind::Static:
        return type.children.empty()
                   ? trim(type.text)
                   : "static[" + substitute_generic_type_ref(type.children[0], params, args) + "]";
    case TypeKind::Template:
        return trim(type.name) + "[" + join_substituted_types(type.children, 0, params, args) + "]";
    case TypeKind::FixedArray:
        return type.children.empty() ? trim(type.text)
                                     : substitute_generic_type_ref(type.children[0], params, args) +
                                           "[" + trim(type.value) + "]";
    case TypeKind::Function: {
        const std::string result =
            type.children.empty() ? "void"
                                  : substitute_generic_type_ref(type.children[0], params, args);
        return "fn(" + join_substituted_types(type.children, 1, params, args) + ") -> " + result;
    }
    case TypeKind::Value:
        return trim(type.value.empty() ? type.text : type.value);
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Unknown:
        return trim(type.text);
    }
    return trim(type.text);
}

std::string join_substituted_types(const std::vector<TypeRef>& types, size_t start,
                                   const std::vector<std::string>& params,
                                   const std::vector<TypeRef>& args) {
    std::ostringstream out;
    for (size_t i = start; i < types.size(); ++i) {
        if (i > start) {
            out << ", ";
        }
        out << substitute_generic_type_ref(types[i], params, args);
    }
    return out.str();
}

bool generic_param_named(const std::vector<std::string>& params, const std::string& name) {
    return std::find(params.begin(), params.end(), name) != params.end();
}

bool infer_generic_binding(const TypeRef& param_type, const TypeRef& arg_type,
                           const std::vector<std::string>& params,
                           std::map<std::string, std::string>& bindings, std::string& error) {
    const std::string param = trim(param_type.name.empty() ? param_type.text : param_type.name);
    const std::string arg = trim(arg_type.name.empty() ? arg_type.text : arg_type.name);
    if (generic_param_named(params, param)) {
        const std::string arg_text = trim(arg_type.text);
        const auto [it, inserted] = bindings.emplace(param, arg_text);
        if (!inserted && it->second != arg_text) {
            error = "conflicting inferred type argument " + param + ": " + it->second + " vs " +
                    arg_text;
            return false;
        }
        return true;
    }
    if (param.empty() || arg.empty()) {
        return true;
    }
    if ((param_type.kind == TypeKind::Pointer || param_type.kind == TypeKind::Reference) &&
        param_type.kind == arg_type.kind && param_type.children.size() == 1 &&
        arg_type.children.size() == 1) {
        return infer_generic_binding(param_type.children.front(), arg_type.children.front(), params,
                                     bindings, error);
    }
    if (param_type.kind != arg_type.kind) {
        return true;
    }
    if (param_type.kind == TypeKind::Template) {
        if (trim(param_type.name) != trim(arg_type.name) ||
            param_type.children.size() != arg_type.children.size()) {
            return true;
        }
        for (size_t i = 0; i < param_type.children.size(); ++i) {
            if (!infer_generic_binding(param_type.children[i], arg_type.children[i], params,
                                       bindings, error)) {
                return false;
            }
        }
        return true;
    }

    if (param_type.kind == TypeKind::FixedArray) {
        if (param_type.children.size() != 1 || arg_type.children.size() != 1) {
            return true;
        }
        return infer_generic_binding(param_type.children.front(), arg_type.children.front(), params,
                                     bindings, error);
    }

    if (param_type.children.empty() || param_type.children.size() != arg_type.children.size()) {
        return true;
    }
    for (size_t i = 0; i < param_type.children.size(); ++i) {
        if (!infer_generic_binding(param_type.children[i], arg_type.children[i], params, bindings,
                                   error)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string template_args_lookup_text(const Expr& expr) {
    std::ostringstream out;
    const size_t count = !expr.template_type_args.empty() ? expr.template_type_args.size()
                                                          : expr.template_args.size();
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << (!expr.template_type_args.empty() ? expr.template_type_args[i].text
                                                 : expr.template_args[i].text);
    }
    return out.str();
}

std::vector<TypeRef> template_type_refs(const Expr& expr) {
    if (!expr.template_type_args.empty()) {
        return expr.template_type_args;
    }
    std::vector<TypeRef> out;
    out.reserve(expr.template_args.size());
    for (const Expr& arg : expr.template_args) {
        out.push_back(parse_type_text(arg.text, arg.location));
    }
    return out;
}

std::optional<std::vector<TypeRef>>
infer_generic_call_type_args(const FunctionScope& scope, const FunctionDecl& fn,
                             const std::string& callee, const std::vector<Expr>& args,
                             const SourceLocation* location,
                             const GenericInferCallbacks& callbacks) {
    if (fn.generic_params.empty()) {
        return std::nullopt;
    }
    if (location != nullptr && args.size() != fn.params.size()) {
        sema_fail(*location, "function " + callee + " expects " + std::to_string(fn.params.size()) +
                                 " arguments, got " + std::to_string(args.size()));
    }
    if (args.size() != fn.params.size()) {
        return std::nullopt;
    }
    std::map<std::string, std::string> bindings;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const std::string got = callbacks.infer_expr(scope, args[i], location);
        std::string error;
        if (!infer_generic_binding(fn.params[i].type_ref, parse_type_text(got), fn.generic_params,
                                   bindings, error)) {
            if (location != nullptr) {
                sema_fail(node_location(*location, args[i]), error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    std::vector<TypeRef> out;
    out.reserve(fn.generic_params.size());
    for (const std::string& param : fn.generic_params) {
        const auto binding = bindings.find(param);
        if (binding == bindings.end() || binding->second.empty() || binding->second == "auto" ||
            binding->second == "list" || binding->second == "dict" || binding->second == "set") {
            if (location != nullptr) {
                sema_fail(*location, "cannot infer type argument " + param + " for " + callee);
            }
            return std::nullopt;
        }
        out.push_back(
            parse_type_text(binding->second, location == nullptr ? fn.location : *location));
    }
    return out;
}

FunctionSignature instantiate_generic_signature(const FunctionDecl& fn,
                                                const std::vector<TypeRef>& args) {
    FunctionSignature signature;
    signature.return_type =
        fn.return_type.empty()
            ? "void"
            : substitute_generic_type_ref(fn.return_type_ref, fn.generic_params, args);
    for (const ParamDecl& param : fn.params) {
        signature.params.push_back(
            substitute_generic_type_ref(param.type_ref, fn.generic_params, args));
    }
    return signature;
}

ClassDecl instantiate_generic_class(ClassDecl klass, const std::vector<TypeRef>& args,
                                    const std::string& instantiated_name) {
    klass.name = instantiated_name;
    for (FieldDecl& field : klass.fields) {
        field.type = substitute_generic_type_ref(field.type_ref, klass.generic_params, args);
        field.type_ref = parse_type_text(field.type, field.location);
    }
    for (ConstDecl& field : klass.static_fields) {
        field.type = substitute_generic_type_ref(field.type_ref, klass.generic_params, args);
        field.type_ref = parse_type_text(field.type, field.location);
    }
    for (ConstDecl& constant : klass.constants) {
        constant.type = substitute_generic_type_ref(constant.type_ref, klass.generic_params, args);
        constant.type_ref = parse_type_text(constant.type, constant.location);
    }
    for (FunctionDecl& method : klass.methods) {
        if (!method.return_type.empty()) {
            method.return_type =
                substitute_generic_type_ref(method.return_type_ref, klass.generic_params, args);
            method.return_type_ref = parse_type_text(method.return_type, method.location);
        }
        for (ParamDecl& param : method.params) {
            param.type = substitute_generic_type_ref(param.type_ref, klass.generic_params, args);
            param.type_ref = parse_type_text(param.type, param.location);
        }
    }
    return klass;
}

std::string join_type_ref_texts(const std::vector<TypeRef>& types) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << types[i].text;
    }
    return out.str();
}

std::string template_method_name(const Expr& expr, const std::string& callee_base,
                                 size_t method_dot) {
    std::ostringstream out;
    out << trim(callee_base.substr(method_dot + 1)) << "[" << template_args_lookup_text(expr)
        << "]";
    return out.str();
}

bool known_template_constructor_type(const FunctionScope& scope, const std::string& callee) {
    const std::string base = base_type(callee);
    if (base.find('.') != std::string::npos || base.find("::") != std::string::npos) {
        return scope.symbols.types.contains(base) || scope.symbols.native_classes.contains(base) ||
               scope.symbols.classes.contains(resolve_alias(scope.symbols, callee));
    }
    return known_type(scope.symbols, callee) ||
           scope.symbols.classes.contains(resolve_alias(scope.symbols, callee));
}

} // namespace dudu
