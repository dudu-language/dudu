#include "dudu/sema_function_type.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <sstream>

namespace dudu {

std::string function_type(const FunctionSignature& signature) {
    std::ostringstream out;
    out << "fn(";
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << signature.params[i];
    }
    out << ") -> " << signature.return_type;
    return out.str();
}

bool parse_function_type(std::string type, FunctionSignature& out) {
    return parse_function_type(parse_type_text(type), out);
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
    out.return_type = function->children.front().text.empty()
                          ? "void"
                          : substitute_type_ref_text(function->children.front(), {});
    for (size_t i = 1; i < function->children.size(); ++i) {
        out.params.push_back(substitute_type_ref_text(function->children[i], {}));
    }
    if (out.return_type.empty()) {
        out.return_type = "void";
    }
    return true;
}

} // namespace dudu
