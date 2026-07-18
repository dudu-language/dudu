#include "dudu/sema/sema_function_type.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_context.hpp"

#include <set>
#include <sstream>
#include <utility>

namespace dudu {
namespace {

bool missing_type_ref(const TypeRef& type) {
    return !has_type_ref(type);
}

bool parse_function_type_or_alias_impl(const Symbols& symbols, const TypeRef& type,
                                       std::set<std::string>& seen_aliases,
                                       FunctionSignature& out) {
    if (parse_function_type(type, out)) {
        return true;
    }
    if ((type.kind != TypeKind::Named && type.kind != TypeKind::Qualified) ||
        !seen_aliases.insert(type.name).second) {
        return false;
    }
    const auto alias = symbols.alias_type_refs.find(type.name.str());
    return alias != symbols.alias_type_refs.end() &&
           parse_function_type_or_alias_impl(symbols, alias->second, seen_aliases, out);
}

} // namespace

FunctionSignature function_signature_from_decl(const FunctionDecl& fn) {
    FunctionSignature signature;
    std::vector<TypeRef> params;
    params.reserve(fn.params.size());
    for (const ParamDecl& param : fn.params) {
        params.push_back(param.type_ref);
    }
    set_signature_param_types(signature, std::move(params));
    set_signature_return_type(signature, function_return_type_ref(fn));
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (fn.params[i].variadic) {
            signature.variadic = true;
            signature.variadic_param_index = static_cast<int>(i);
            break;
        }
    }
    return signature;
}

TypeRef function_type_ref(const FunctionSignature& signature, SourceLocation location) {
    TypeRef out;
    out.kind = TypeKind::Function;
    out.name = "fn";
    out.location = location;
    const size_t param_count = signature_param_count(signature);
    out.children.reserve(param_count + 1);
    out.children.push_back(signature_return_type_ref(signature));
    for (size_t i = 0; i < param_count; ++i) {
        out.children.push_back(signature_param_type_ref(signature, i));
    }
    return out;
}

void set_signature_param_types(FunctionSignature& signature, std::vector<TypeRef> types) {
    signature.param_type_refs = std::move(types);
}

void set_signature_return_type(FunctionSignature& signature, TypeRef type) {
    if (!has_type_ref(type)) {
        type = void_type_ref();
    }
    signature.return_type_ref = std::move(type);
}

size_t signature_param_count(const FunctionSignature& signature) {
    return signature.param_type_refs.size();
}

size_t signature_min_arg_count(const FunctionSignature& signature) {
    const size_t param_count = signature_param_count(signature);
    if (signature.min_params >= 0) {
        return std::min(param_count, static_cast<size_t>(signature.min_params));
    }
    return signature.variadic && param_count > 0 ? param_count - 1 : param_count;
}

TypeRef signature_param_type_ref(const FunctionSignature& signature, size_t index) {
    if (index < signature.param_type_refs.size() &&
        has_type_ref(signature.param_type_refs[index])) {
        return signature.param_type_refs[index];
    }
    return {};
}

TypeRef signature_return_type_ref(const FunctionSignature& signature) {
    if (has_type_ref(signature.return_type_ref)) {
        return signature.return_type_ref;
    }
    return void_type_ref();
}

size_t signature_variadic_param_index(const FunctionSignature& signature) {
    const size_t param_count = signature_param_count(signature);
    if (!signature.variadic || param_count == 0) {
        return param_count;
    }
    if (signature.variadic_param_index >= 0 &&
        static_cast<size_t>(signature.variadic_param_index) < param_count) {
        return static_cast<size_t>(signature.variadic_param_index);
    }
    return param_count - 1;
}

size_t signature_fixed_param_count(const FunctionSignature& signature, size_t arg_count) {
    const size_t param_count = signature_param_count(signature);
    if (!signature.variadic) {
        return param_count;
    }
    const size_t variadic_index = signature_variadic_param_index(signature);
    const size_t fixed_after = param_count - variadic_index - 1;
    if (arg_count < fixed_after) {
        return param_count;
    }
    const size_t variadic_arg_count = arg_count + 1 - param_count;
    return param_count + variadic_arg_count - 1;
}

size_t signature_param_index_for_arg(const FunctionSignature& signature, size_t arg_index,
                                     size_t arg_count) {
    if (!signature.variadic) {
        return arg_index;
    }
    const size_t param_count = signature_param_count(signature);
    const size_t variadic_index = signature_variadic_param_index(signature);
    const size_t fixed_after = param_count - variadic_index - 1;
    const size_t variadic_arg_count = arg_count + 1 - param_count;
    if (arg_index < variadic_index) {
        return arg_index;
    }
    if (arg_index < variadic_index + variadic_arg_count) {
        return variadic_index;
    }
    return param_count - fixed_after + (arg_index - variadic_index - variadic_arg_count);
}

std::string function_type(const FunctionSignature& signature) {
    std::ostringstream out;
    out << "fn(";
    const size_t param_count = signature_param_count(signature);
    for (size_t i = 0; i < param_count; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << type_ref_text(signature_param_type_ref(signature, i));
    }
    out << ") -> " << type_ref_text(signature_return_type_ref(signature));
    return out.str();
}

bool parse_function_type(const TypeRef& type, FunctionSignature& out) {
    const TypeRef* function = &type;
    if (type.kind == TypeKind::Template && type.children.size() == 1 &&
        type.children.front().kind == TypeKind::Function) {
        function = &type.children.front();
    }
    if (function->kind != TypeKind::Function || function->children.empty()) {
        return false;
    }
    set_signature_return_type(out, missing_type_ref(function->children.front())
                                       ? void_type_ref(type.location)
                                       : function->children.front());
    std::vector<TypeRef> param_types;
    param_types.reserve(function->children.size() - 1);
    for (size_t i = 1; i < function->children.size(); ++i) {
        param_types.push_back(function->children[i]);
    }
    set_signature_param_types(out, std::move(param_types));
    return true;
}

bool parse_function_type_or_alias(const Symbols& symbols, const TypeRef& type,
                                  FunctionSignature& out) {
    std::set<std::string> seen_aliases;
    return parse_function_type_or_alias_impl(symbols, type, seen_aliases, out);
}

} // namespace dudu
