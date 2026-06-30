#include "dudu/core/ast_type.hpp"

#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/codegen/cpp_lower.hpp"

#include <sstream>
#include <utility>

namespace dudu {
namespace {

std::string join_substituted_types(const std::vector<TypeRef>& types, size_t start,
                                   const std::map<std::string, std::string>& substitutions) {
    std::ostringstream out;
    for (size_t i = start; i < types.size(); ++i) {
        if (i > start) {
            out << ", ";
        }
        out << substitute_type_ref_text(types[i], substitutions);
    }
    return out.str();
}

std::string join_fixed_array_shape_refs(const TypeRef& type,
                                        const std::map<std::string, std::string>& substitutions) {
    std::ostringstream out;
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            out << ", ";
        }
        out << substitute_type_ref_text(type.children[i], substitutions);
    }
    return out.str();
}

std::string join_shape_refs(const TypeRef& type,
                            const std::map<std::string, std::string>& substitutions) {
    std::ostringstream out;
    for (size_t i = 1; i < type.children.size(); ++i) {
        if (i > 1) {
            out << ", ";
        }
        out << substitute_type_ref_text(type.children[i], substitutions);
    }
    return out.str();
}

std::string substitute_wrapper(std::string_view name, const TypeRef& type,
                               const std::map<std::string, std::string>& substitutions) {
    if (type.children.empty()) {
        throw CompileError(type.location, "malformed structured type node: " +
                                              std::string(type_kind_name(type.kind)) +
                                              " is missing its child type");
    }
    return std::string(name) + "[" + substitute_type_ref_text(type.children[0], substitutions) +
           "]";
}

std::string substitution_lookup_key(const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Template:
        return trim_copy(type.name);
    default:
        return {};
    }
}

} // namespace

std::vector<TypeRef> template_type_arg_refs(const TypeRef& type, std::string_view name) {
    if (type.kind != TypeKind::Template || type.name != name) {
        return {};
    }
    return type.children;
}

std::vector<TypeRef>
template_type_arg_refs_with_aliases(const TypeRef& type, std::string_view name,
                                    const std::map<std::string, TypeRef>& aliases) {
    std::vector<TypeRef> refs = template_type_arg_refs(type, name);
    if (!refs.empty()) {
        return refs;
    }

    const std::string key = substitution_lookup_key(type);
    const auto found = !key.empty() ? aliases.find(key) : aliases.end();
    if (found == aliases.end()) {
        return {};
    }
    return template_type_arg_refs(found->second, name);
}

std::optional<TypeRef> unary_type_child_ref(const TypeRef& type, TypeKind kind) {
    if (type.kind != kind || type.children.size() != 1) {
        return std::nullopt;
    }
    return type.children.front();
}

std::optional<TypeRef> unary_type_child_ref(const TypeRef& type,
                                            std::initializer_list<TypeKind> kinds) {
    if (type.children.size() != 1) {
        return std::nullopt;
    }
    for (const TypeKind kind : kinds) {
        if (type.kind == kind) {
            return type.children.front();
        }
    }
    return std::nullopt;
}

std::string type_ref_head_name(const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Template:
        return trim_copy(type.name);
    case TypeKind::Value:
        return trim_copy(type.value);
    case TypeKind::Function:
        return "fn";
    case TypeKind::Pointer:
        return "*";
    case TypeKind::Reference:
        return "&";
    case TypeKind::Const:
        return "const";
    case TypeKind::Volatile:
        return "volatile";
    case TypeKind::Atomic:
        return "atomic";
    case TypeKind::Device:
        return "device";
    case TypeKind::Storage:
        return "storage";
    case TypeKind::Shared:
        return "shared";
    case TypeKind::Static:
        return "static";
    case TypeKind::FixedArray:
        return "array";
    case TypeKind::Shaped:
        return type.children.empty() ? std::string{} : type_ref_head_name(type.children.front());
    case TypeKind::PackExpansion:
        return "...";
    case TypeKind::Unknown:
        return {};
    }
    return {};
}

bool type_ref_contains_kind(const TypeRef& type, TypeKind kind) {
    if (type.kind == kind) {
        return true;
    }
    for (const TypeRef& child : type.children) {
        if (type_ref_contains_kind(child, kind)) {
            return true;
        }
    }
    return false;
}

std::string substitute_type_ref_text(const TypeRef& type,
                                     const std::map<std::string, std::string>& substitutions) {
    const std::string key = substitution_lookup_key(type);
    if (!key.empty()) {
        if (const auto found = substitutions.find(key); found != substitutions.end()) {
            return found->second;
        }
    }

    switch (type.kind) {
    case TypeKind::Pointer:
        if (type.children.empty()) {
            throw CompileError(type.location,
                               "malformed structured type node: pointer is missing its child type");
        }
        return "*" + substitute_type_ref_text(type.children[0], substitutions);
    case TypeKind::Reference:
        if (type.children.empty()) {
            throw CompileError(
                type.location,
                "malformed structured type node: reference is missing its child type");
        }
        return "&" + substitute_type_ref_text(type.children[0], substitutions);
    case TypeKind::Const:
        return substitute_wrapper("const", type, substitutions);
    case TypeKind::Volatile:
        return substitute_wrapper("volatile", type, substitutions);
    case TypeKind::Atomic:
        return substitute_wrapper("atomic", type, substitutions);
    case TypeKind::Device:
        return substitute_wrapper("device", type, substitutions);
    case TypeKind::Storage:
        return substitute_wrapper("storage", type, substitutions);
    case TypeKind::Shared:
        return substitute_wrapper("shared", type, substitutions);
    case TypeKind::Static:
        return substitute_wrapper("static", type, substitutions);
    case TypeKind::Template:
        if (type.name.find("::") != std::string::npos) {
            return trim_copy(type.name) + "<" +
                   join_substituted_types(type.children, 0, substitutions) + ">";
        }
        return trim_copy(type.name) + "[" +
               join_substituted_types(type.children, 0, substitutions) + "]";
    case TypeKind::FixedArray:
        if (type.children.empty()) {
            throw CompileError(
                type.location,
                "malformed structured type node: fixed_array is missing its child type");
        }
        return substitute_type_ref_text(type.children[0], substitutions) + "[" +
               join_fixed_array_shape_refs(type, substitutions) + "]";
    case TypeKind::Shaped:
        if (type.children.empty()) {
            throw CompileError(type.location,
                               "malformed structured type node: shaped is missing its base type");
        }
        return substitute_type_ref_text(type.children[0], substitutions) + "[" +
               join_shape_refs(type, substitutions) + "]";
    case TypeKind::Function: {
        const std::string result = type.children.empty()
                                       ? "void"
                                       : substitute_type_ref_text(type.children[0], substitutions);
        return "fn(" + join_substituted_types(type.children, 1, substitutions) + ") -> " + result;
    }
    case TypeKind::PackExpansion:
        if (type.children.size() != 1) {
            throw CompileError(
                type.location,
                "malformed structured type node: pack_expansion is missing its child type");
        }
        return substitute_type_ref_text(type.children[0], substitutions) + "...";
    case TypeKind::Value:
        return trim_copy(type.value);
    case TypeKind::Named:
    case TypeKind::Qualified:
        return trim_copy(type.name);
    case TypeKind::Unknown:
        return {};
    }
    return {};
}

std::string type_ref_text(const TypeRef& type) {
    return substitute_type_ref_text(type, {});
}

bool has_type_ref(const TypeRef& type) {
    return type.kind != TypeKind::Unknown || type.malformed || !trim_copy(type.name).empty() ||
           !trim_copy(type.value).empty() || !type.children.empty();
}

bool has_expr_type_ref(const Expr& expr) {
    return expr.type_ref != nullptr && has_type_ref(*expr.type_ref);
}

const TypeRef& expr_type_ref(const Expr& expr) {
    static const TypeRef empty;
    return expr.type_ref == nullptr ? empty : *expr.type_ref;
}

void set_expr_type_ref(Expr& expr, TypeRef type) {
    if (!has_type_ref(type)) {
        expr.type_ref.reset();
        return;
    }
    expr.type_ref = std::make_unique<TypeRef>(std::move(type));
}

bool has_stmt_type_ref(const Stmt& stmt) {
    return stmt.type_ref != nullptr && has_type_ref(*stmt.type_ref);
}

const TypeRef& stmt_type_ref(const Stmt& stmt) {
    static const TypeRef empty;
    return stmt.type_ref == nullptr ? empty : *stmt.type_ref;
}

void set_stmt_type_ref(Stmt& stmt, TypeRef type) {
    if (!has_type_ref(type)) {
        stmt.type_ref.reset();
        return;
    }
    stmt.type_ref = std::make_shared<TypeRef>(std::move(type));
}

bool type_ref_is_name(const TypeRef& type, std::string_view name) {
    return type.kind == TypeKind::Named && trim_view(type.name) == name;
}

bool type_ref_is_auto(const TypeRef& type) {
    return type_ref_is_name(type, "auto");
}

bool type_ref_is_void(const TypeRef& type) {
    return type_ref_is_name(type, "void");
}

bool type_ref_same_shape(const TypeRef& left, const TypeRef& right) {
    if (left.kind != right.kind || left.children.size() != right.children.size() ||
        left.malformed != right.malformed || left.name != right.name) {
        return false;
    }
    if (left.kind == TypeKind::Value && trim_copy(left.value) != trim_copy(right.value)) {
        return false;
    }
    for (size_t i = 0; i < left.children.size(); ++i) {
        if (!type_ref_same_shape(left.children[i], right.children[i])) {
            return false;
        }
    }
    return true;
}

bool type_ref_equivalent(const TypeRef& left, const TypeRef& right) {
    if (left.kind != right.kind || left.children.size() != right.children.size()) {
        return false;
    }

    switch (left.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Template:
        if (type_ref_head_name(left) != type_ref_head_name(right)) {
            return false;
        }
        break;
    case TypeKind::Value:
        if (trim_copy(left.value) != trim_copy(right.value)) {
            return false;
        }
        break;
    case TypeKind::FixedArray:
    case TypeKind::Shaped:
        break;
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Const:
    case TypeKind::Volatile:
    case TypeKind::Atomic:
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
    case TypeKind::Static:
    case TypeKind::Function:
    case TypeKind::PackExpansion:
        break;
    case TypeKind::Unknown:
        return false;
    }

    for (size_t i = 0; i < left.children.size(); ++i) {
        if (!type_ref_equivalent(left.children[i], right.children[i])) {
            return false;
        }
    }
    return true;
}

TypeRef named_type_ref(std::string name, SourceLocation location) {
    TypeRef type;
    type.kind = name.find('.') == std::string::npos && name.find("::") == std::string::npos
                    ? TypeKind::Named
                    : TypeKind::Qualified;
    type.name = std::move(name);
    type.location = location;
    return type;
}

TypeRef void_type_ref(SourceLocation location) {
    return named_type_ref("void", location);
}

TypeRef wrapped_type_ref(TypeKind kind, TypeRef child, SourceLocation location) {
    TypeRef type;
    type.kind = kind;
    type.location = location;
    type.range = child.range;
    type.children.push_back(std::move(child));
    return type;
}

TypeRef pack_expansion_type_ref(TypeRef child, SourceLocation location) {
    return wrapped_type_ref(TypeKind::PackExpansion, std::move(child), location);
}

bool function_has_receiver_type(const FunctionDecl& fn) {
    return has_type_ref(fn.receiver_type_ref);
}

bool function_has_return_type(const FunctionDecl& fn) {
    return has_type_ref(fn.return_type_ref);
}

TypeRef function_return_type_ref(const FunctionDecl& fn) {
    return function_has_return_type(fn) ? fn.return_type_ref : void_type_ref(fn.location);
}

std::vector<TypeRef> native_function_param_type_refs(const NativeFunctionDecl& fn) {
    if (!fn.param_type_refs.empty()) {
        return fn.param_type_refs;
    }
    return std::vector<TypeRef>(fn.param_native_spellings.size(),
                                named_type_ref("auto", fn.location));
}

TypeRef native_function_return_type_ref(const NativeFunctionDecl& fn) {
    if (has_type_ref(fn.return_type_ref)) {
        return fn.return_type_ref;
    }
    return named_type_ref("auto", fn.location);
}

TypeRef native_type_alias_type_ref(const NativeTypeDecl& type) {
    if (has_type_ref(type.type_ref)) {
        return type.type_ref;
    }
    return named_type_ref(type.native_spelling.empty() ? type.name : type.native_spelling,
                          type.location);
}

TypeRef native_value_type_ref(const NativeValueDecl& value) {
    if (has_type_ref(value.type_ref)) {
        return value.type_ref;
    }
    return value.native_spelling.empty() ? named_type_ref("auto", value.location)
                                         : named_type_ref(value.native_spelling, value.location);
}

std::string native_type_alias_type_text(const NativeTypeDecl& type) {
    if (!type.native_spelling.empty()) {
        return type.native_spelling;
    }
    return type_ref_text(native_type_alias_type_ref(type));
}

std::string native_value_type_text(const NativeValueDecl& value) {
    return type_ref_text(native_value_type_ref(value));
}

TypeRef substitute_type_ref(const TypeRef& type,
                            const std::map<std::string, TypeRef>& substitutions) {
    const std::string key = substitution_lookup_key(type);
    if (!key.empty()) {
        if (const auto found = substitutions.find(key); found != substitutions.end()) {
            TypeRef out = found->second;
            out.location = type.location;
            return out;
        }
    }

    TypeRef out = type;
    for (TypeRef& child : out.children) {
        child = substitute_type_ref(child, substitutions);
    }
    if ((out.kind == TypeKind::FixedArray || out.kind == TypeKind::Shaped) &&
        out.children.size() > 1) {
        std::ostringstream value;
        for (size_t i = 1; i < out.children.size(); ++i) {
            if (i > 1) {
                value << ", ";
            }
            value << substitute_type_ref_text(out.children[i], {});
        }
        out.value = value.str();
    }
    return out;
}

} // namespace dudu
