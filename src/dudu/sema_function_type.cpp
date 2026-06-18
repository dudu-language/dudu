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
    out.children.reserve(signature.params.size() + 1);
    out.children.push_back(signature_return_type_ref(signature));
    for (size_t i = 0; i < signature.params.size(); ++i) {
        out.children.push_back(signature_param_type_ref(signature, i));
    }
    out.text = substitute_type_ref_text(out, {});
    return out;
}

void set_signature_param_types(FunctionSignature& signature, std::vector<TypeRef> types) {
    signature.param_type_refs = std::move(types);
    signature.params.clear();
    signature.params.reserve(signature.param_type_refs.size());
    for (const TypeRef& type : signature.param_type_refs) {
        signature.params.push_back(substitute_type_ref_text(type, {}));
    }
}

void set_signature_return_type(FunctionSignature& signature, TypeRef type) {
    if (!has_type_ref(type)) {
        type = parse_type_text("void");
    }
    signature.return_type_ref = std::move(type);
    signature.return_type = substitute_type_ref_text(signature.return_type_ref, {});
}

TypeRef signature_param_type_ref(const FunctionSignature& signature, size_t index) {
    if (index < signature.param_type_refs.size() &&
        has_type_ref(signature.param_type_refs[index])) {
        return signature.param_type_refs[index];
    }
    if (index < signature.params.size()) {
        return parse_type_text(signature.params[index]);
    }
    return {};
}

TypeRef signature_return_type_ref(const FunctionSignature& signature) {
    if (has_type_ref(signature.return_type_ref)) {
        return signature.return_type_ref;
    }
    return parse_type_text(signature.return_type.empty() ? "void" : signature.return_type);
}

std::string signature_return_type_text(const FunctionSignature& signature) {
    return substitute_type_ref_text(signature_return_type_ref(signature), {});
}

std::string function_type(const FunctionSignature& signature) {
    std::ostringstream out;
    out << "fn(";
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << substitute_type_ref_text(signature_param_type_ref(signature, i), {});
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
    out.params.clear();
    out.param_type_refs.clear();
    out.return_type = missing_type_ref(function->children.front())
                          ? "void"
                          : substitute_type_ref_text(function->children.front(), {});
    out.return_type_ref = missing_type_ref(function->children.front())
                              ? parse_type_text("void", type.location)
                              : function->children.front();
    for (size_t i = 1; i < function->children.size(); ++i) {
        out.params.push_back(substitute_type_ref_text(function->children[i], {}));
        out.param_type_refs.push_back(function->children[i]);
    }
    if (out.return_type.empty()) {
        out.return_type = "void";
    }
    return true;
}

} // namespace dudu
