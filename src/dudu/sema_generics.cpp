#include "dudu/sema_generics.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/sema_common.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <utility>

namespace dudu {

namespace {

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string replace_type_parameter(std::string type, const std::string& from,
                                   const std::string& to) {
    if (from.empty() || to.empty()) {
        return type;
    }
    size_t pos = type.find(from);
    while (pos != std::string::npos) {
        const bool left_ok = pos == 0 || !is_identifier_char(type[pos - 1]);
        const size_t end = pos + from.size();
        const bool right_ok = end == type.size() || !is_identifier_char(type[end]);
        if (left_ok && right_ok) {
            type.replace(pos, from.size(), to);
            pos = type.find(from, pos + to.size());
        } else {
            pos = type.find(from, end);
        }
    }
    return type;
}

std::string substitute_generic_type(std::string type, const std::vector<std::string>& params,
                                    const std::vector<TypeRef>& args) {
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        type = replace_type_parameter(std::move(type), params[i], args[i].text);
    }
    return type;
}

bool generic_param_named(const std::vector<std::string>& params, const std::string& name) {
    return std::find(params.begin(), params.end(), name) != params.end();
}

std::string generic_base_name(const std::string& type) {
    const size_t open = type.find('[');
    return open == std::string::npos ? trim(type) : trim(type.substr(0, open));
}

std::vector<std::string> generic_type_args(const std::string& type) {
    const size_t open = type.find('[');
    if (open == std::string::npos || type.empty() || type.back() != ']') {
        return {};
    }
    return split_top_level_args(type.substr(open + 1, type.size() - open - 2));
}

bool infer_generic_binding(const std::string& param_type, const std::string& arg_type,
                           const std::vector<std::string>& params,
                           std::map<std::string, std::string>& bindings, std::string& error) {
    const std::string param = trim(param_type);
    const std::string arg = trim(arg_type);
    if (generic_param_named(params, param)) {
        const auto [it, inserted] = bindings.emplace(param, arg);
        if (!inserted && it->second != arg) {
            error =
                "conflicting inferred type argument " + param + ": " + it->second + " vs " + arg;
            return false;
        }
        return true;
    }
    if (param.empty() || arg.empty()) {
        return true;
    }
    if ((param.front() == '*' || param.front() == '&') && param.front() == arg.front()) {
        return infer_generic_binding(param.substr(1), arg.substr(1), params, bindings, error);
    }
    const std::string param_base = generic_base_name(param);
    const std::string arg_base = generic_base_name(arg);
    if (param_base != arg_base) {
        return true;
    }
    const std::vector<std::string> param_args = generic_type_args(param);
    const std::vector<std::string> arg_args = generic_type_args(arg);
    if (param_args.empty() || param_args.size() != arg_args.size()) {
        return true;
    }
    for (size_t i = 0; i < param_args.size(); ++i) {
        if (!infer_generic_binding(param_args[i], arg_args[i], params, bindings, error)) {
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
        if (!infer_generic_binding(fn.params[i].type, got, fn.generic_params, bindings, error)) {
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
    signature.return_type = substitute_generic_type(
        fn.return_type.empty() ? "void" : fn.return_type, fn.generic_params, args);
    for (const ParamDecl& param : fn.params) {
        signature.params.push_back(substitute_generic_type(param.type, fn.generic_params, args));
    }
    return signature;
}

ClassDecl instantiate_generic_class(ClassDecl klass, const std::vector<TypeRef>& args,
                                    const std::string& instantiated_name) {
    klass.name = instantiated_name;
    for (FieldDecl& field : klass.fields) {
        field.type = substitute_generic_type(std::move(field.type), klass.generic_params, args);
    }
    for (ConstDecl& field : klass.static_fields) {
        field.type = substitute_generic_type(std::move(field.type), klass.generic_params, args);
    }
    for (ConstDecl& constant : klass.constants) {
        constant.type =
            substitute_generic_type(std::move(constant.type), klass.generic_params, args);
    }
    for (FunctionDecl& method : klass.methods) {
        method.return_type =
            substitute_generic_type(std::move(method.return_type), klass.generic_params, args);
        for (ParamDecl& param : method.params) {
            param.type = substitute_generic_type(std::move(param.type), klass.generic_params, args);
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
