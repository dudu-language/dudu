#include "dudu/sema_function_type.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <sstream>
#include <utility>

namespace dudu {
namespace {

bool missing_type_ref(const TypeRef& type) {
    return !has_type_ref(type);
}

} // namespace

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

std::string signature_param_type_text(const FunctionSignature& signature, size_t index) {
    return substitute_type_ref_text(signature_param_type_ref(signature, index), {});
}

std::string signature_return_type_text(const FunctionSignature& signature) {
    return substitute_type_ref_text(signature_return_type_ref(signature), {});
}

std::string function_type(const FunctionSignature& signature) {
    std::ostringstream out;
    out << "fn(";
    const size_t param_count = signature_param_count(signature);
    for (size_t i = 0; i < param_count; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << signature_param_type_text(signature, i);
    }
    out << ") -> " << signature_return_type_text(signature);
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

} // namespace dudu
