#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_type_internal.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/parser/ast_parse_utils.hpp"

#include <sstream>

namespace dudu {
namespace {

[[noreturn]] void malformed_type_ref(const TypeRef& type) {
    throw CompileError(type.location,
                       "malformed structured type node: " + std::string(type_kind_name(type.kind)) +
                           " is missing its child type");
}

} // namespace

std::string lower_cpp_function_type(const TypeRef& type, bool pointer) {
    std::ostringstream signature;
    const std::string result = type.children.empty() ? "void" : lower_cpp_type(type.children[0]);
    signature << result << '(';
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            signature << ", ";
        }
        signature << lower_cpp_type(type.children[i]);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

std::string lower_cpp_function_type(const TypeRef& type, bool pointer,
                                    const std::vector<std::string>& namespace_aliases) {
    std::ostringstream signature;
    const std::string result =
        type.children.empty() ? "void" : lower_cpp_type(type.children[0], namespace_aliases);
    signature << result << '(';
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            signature << ", ";
        }
        signature << lower_cpp_type(type.children[i], namespace_aliases);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

std::string lower_cpp_function_type(const TypeRef& type, bool pointer,
                                    const std::vector<std::string>& namespace_aliases,
                                    const CppEmitOptions& options) {
    std::ostringstream signature;
    const std::string result = type.children.empty()
                                   ? "void"
                                   : lower_cpp_type(type.children[0], namespace_aliases, options);
    signature << result << '(';
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            signature << ", ";
        }
        signature << lower_cpp_type(type.children[i], namespace_aliases, options);
    }
    signature << ')';
    return pointer ? "std::add_pointer_t<" + signature.str() + ">" : signature.str();
}

namespace {

std::string lower_top_level_const_type(const TypeRef& type) {
    if (type.kind == TypeKind::Pointer && !type.children.empty()) {
        return lower_cpp_type(type.children.front()) + "* const";
    }
    if (type.kind == TypeKind::Reference) {
        return lower_cpp_type(type);
    }
    return "const " + lower_cpp_type(type);
}

std::string lower_top_level_const_type(const TypeRef& type,
                                       const std::vector<std::string>& namespace_aliases) {
    if (type.kind == TypeKind::Pointer && !type.children.empty()) {
        return lower_cpp_type(type.children.front(), namespace_aliases) + "* const";
    }
    if (type.kind == TypeKind::Reference) {
        return lower_cpp_type(type, namespace_aliases);
    }
    return "const " + lower_cpp_type(type, namespace_aliases);
}

std::string lower_top_level_const_type(const TypeRef& type,
                                       const std::vector<std::string>& namespace_aliases,
                                       const CppEmitOptions& options) {
    if (type.kind == TypeKind::Pointer && !type.children.empty()) {
        return lower_cpp_type(type.children.front(), namespace_aliases, options) + "* const";
    }
    if (type.kind == TypeKind::Reference) {
        return lower_cpp_type(type, namespace_aliases, options);
    }
    return "const " + lower_cpp_type(type, namespace_aliases, options);
}

} // namespace

std::string lower_cpp_type(const TypeRef& type) {
    if (!has_type_ref(type)) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
        return lower_cpp_type_name(type_ref_head_name(type));
    case TypeKind::Qualified:
        return lower_cpp_type_name(type_ref_text(type));
    case TypeKind::Value:
        return type_ref_head_name(type);
    case TypeKind::Template:
        return lower_template_type(type);
    case TypeKind::Associated:
        if (type.children.size() != 1) {
            malformed_type_ref(type);
        }
        return "typename " + lower_cpp_type(type.children.front()) + "::" + std::string(type.name);
    case TypeKind::Pointer:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0]) + "*";
    case TypeKind::Reference:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0]) +
               (type.reference_kind == ReferenceKind::Rvalue ? "&&" : "&");
    case TypeKind::Const:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_top_level_const_type(type.children[0]);
    case TypeKind::Volatile:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return "volatile " + lower_cpp_type(type.children[0]);
    case TypeKind::Atomic:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return "std::atomic<" + lower_cpp_type(type.children[0]) + ">";
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0]);
    case TypeKind::Static:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0]);
    case TypeKind::FixedArray:
        return lower_fixed_array_type(type);
    case TypeKind::Shaped:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children.front());
    case TypeKind::Function:
        return lower_cpp_function_type(type, true);
    case TypeKind::PackExpansion:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children.front()) + "...";
    case TypeKind::Unknown:
        malformed_type_ref(type);
    }
    malformed_type_ref(type);
}

std::string lower_cpp_type(const TypeRef& type, const std::vector<std::string>& namespace_aliases) {
    if (namespace_aliases.empty()) {
        return lower_cpp_type(type);
    }
    if (!has_type_ref(type)) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
        return lower_cpp_type_name(type_ref_head_name(type), namespace_aliases);
    case TypeKind::Qualified:
        return lower_cpp_type_name(type_ref_text(type), namespace_aliases);
    case TypeKind::Value:
        return type_ref_head_name(type);
    case TypeKind::Template:
        return lower_template_type(type, namespace_aliases);
    case TypeKind::Associated:
        if (type.children.size() != 1) {
            malformed_type_ref(type);
        }
        return "typename " + lower_cpp_type(type.children.front(), namespace_aliases) +
               "::" + std::string(type.name);
    case TypeKind::Pointer:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0], namespace_aliases) + "*";
    case TypeKind::Reference:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0], namespace_aliases) +
               (type.reference_kind == ReferenceKind::Rvalue ? "&&" : "&");
    case TypeKind::Const:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_top_level_const_type(type.children[0], namespace_aliases);
    case TypeKind::Volatile:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return "volatile " + lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::Atomic:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return "std::atomic<" + lower_cpp_type(type.children[0], namespace_aliases) + ">";
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::Static:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0], namespace_aliases);
    case TypeKind::FixedArray:
        return lower_fixed_array_type(type, namespace_aliases);
    case TypeKind::Shaped:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children.front(), namespace_aliases);
    case TypeKind::Function:
        return lower_cpp_function_type(type, true, namespace_aliases);
    case TypeKind::PackExpansion:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children.front(), namespace_aliases) + "...";
    case TypeKind::Unknown:
        malformed_type_ref(type);
    }
    malformed_type_ref(type);
}

std::string lower_cpp_type(const TypeRef& type, const CppEmitOptions& options) {
    return lower_cpp_type(type, {}, options);
}

std::string lower_cpp_type(const TypeRef& type, const std::vector<std::string>& namespace_aliases,
                           const CppEmitOptions& options) {
    if (!options.use_generated_names && options.generated_type_names.empty()) {
        return lower_cpp_type(type, namespace_aliases);
    }
    if (!has_type_ref(type)) {
        return "void";
    }
    switch (type.kind) {
    case TypeKind::Named:
        return lower_cpp_type_name(type_ref_head_name(type), namespace_aliases, options);
    case TypeKind::Qualified:
        return lower_cpp_type_name(type_ref_text(type), namespace_aliases, options);
    case TypeKind::Value:
        return type_ref_head_name(type);
    case TypeKind::Template:
        return lower_template_type(type, namespace_aliases, options);
    case TypeKind::Associated:
        if (type.children.size() != 1) {
            malformed_type_ref(type);
        }
        return "typename " + lower_cpp_type(type.children.front(), namespace_aliases, options) +
               "::" + std::string(type.name);
    case TypeKind::Pointer:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0], namespace_aliases, options) + "*";
    case TypeKind::Reference:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0], namespace_aliases, options) +
               (type.reference_kind == ReferenceKind::Rvalue ? "&&" : "&");
    case TypeKind::Const:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_top_level_const_type(type.children[0], namespace_aliases, options);
    case TypeKind::Volatile:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return "volatile " + lower_cpp_type(type.children[0], namespace_aliases, options);
    case TypeKind::Atomic:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return "std::atomic<" + lower_cpp_type(type.children[0], namespace_aliases, options) + ">";
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0], namespace_aliases, options);
    case TypeKind::Static:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children[0], namespace_aliases, options);
    case TypeKind::FixedArray:
        return lower_fixed_array_type(type, namespace_aliases, options);
    case TypeKind::Shaped:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children.front(), namespace_aliases, options);
    case TypeKind::Function:
        return lower_cpp_function_type(type, true, namespace_aliases, options);
    case TypeKind::PackExpansion:
        if (type.children.empty()) {
            malformed_type_ref(type);
        }
        return lower_cpp_type(type.children.front(), namespace_aliases, options) + "...";
    case TypeKind::Unknown:
        malformed_type_ref(type);
    }
    malformed_type_ref(type);
}

} // namespace dudu
