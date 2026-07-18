#include "dudu/sema/sema_builtin_methods.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

TypeRef auto_type_ref() {
    return named_type_ref("auto");
}

std::optional<TypeRef> first_type_arg_ref(const TypeRef& type) {
    if (const auto arg =
            unary_type_child_ref(type, {TypeKind::Atomic, TypeKind::Const, TypeKind::Volatile,
                                        TypeKind::Device, TypeKind::Storage, TypeKind::Shared})) {
        return *arg;
    }
    if (type.kind == TypeKind::Template && !type.children.empty()) {
        return type.children.front();
    }
    return std::nullopt;
}

bool template_head_is(const TypeRef& type, std::initializer_list<std::string_view> names) {
    if (type.kind != TypeKind::Template) {
        return false;
    }
    for (std::string_view name : names) {
        if (type.name == name) {
            return true;
        }
    }
    return false;
}

bool single_template_type_arg_named(const TypeRef& type,
                                    std::initializer_list<std::string_view> names) {
    return template_head_is(type, names) && type.children.size() == 1;
}

bool type_head_is(const TypeRef& type, std::initializer_list<std::string_view> names) {
    const std::string head = type_ref_head_name(type);
    for (std::string_view name : names) {
        if (head == name) {
            return true;
        }
    }
    return false;
}

void set_return_type(FunctionSignature& signature, const std::string& type) {
    set_signature_return_type(signature, named_type_ref(type));
}

void set_return_type(FunctionSignature& signature, TypeRef type) {
    set_signature_return_type(signature, std::move(type));
}

void set_param_types(FunctionSignature& signature, std::initializer_list<TypeRef> types) {
    std::vector<TypeRef> refs;
    refs.reserve(types.size());
    for (TypeRef type : types) {
        refs.push_back(std::move(type));
    }
    set_signature_param_types(signature, std::move(refs));
}

} // namespace

TypeRef receiver_template_type_ref(const Symbols& symbols, TypeRef type) {
    type = resolve_alias_ref(symbols, std::move(type));
    type = normalize_cpp_type_structure_ref(type);
    while (true) {
        if (type.kind == TypeKind::Shaped && !type.children.empty()) {
            type = type.children.front();
            type = resolve_alias_ref(symbols, std::move(type));
            type = normalize_cpp_type_structure_ref(type);
            continue;
        }
        if (const auto inner = unary_type_child_ref(
                type, {TypeKind::Pointer, TypeKind::Reference, TypeKind::Const, TypeKind::Volatile,
                       TypeKind::Storage, TypeKind::Shared, TypeKind::Device})) {
            type = *inner;
            type = resolve_alias_ref(symbols, std::move(type));
            type = normalize_cpp_type_structure_ref(type);
            continue;
        }
        break;
    }
    const auto klass = symbols.classes.find(type_ref_head_name(type));
    if (klass != symbols.classes.end() && !klass->second->generic_params.empty()) {
        const std::vector<TypeRef> args = generic_args_with_defaults(
            klass->second->generic_params, klass->second->generic_default_args,
            type.kind == TypeKind::Template ? type.children : std::vector<TypeRef>{});
        if (!args.empty()) {
            type.kind = TypeKind::Template;
            type.children = args;
        }
    }
    return type;
}

bool builtin_cpp_method_signature(const Symbols& symbols, const TypeRef& receiver_type,
                                  const std::string& method_name, FunctionSignature& signature) {
    TypeRef templated_ref = receiver_template_type_ref(symbols, receiver_type);
    const bool is_string = type_head_is(templated_ref, {"str", "string", "string_view"});
    if (is_string) {
        if (method_name == "size" || method_name == "length") {
            set_return_type(signature, "usize");
            return true;
        }
        if (method_name == "empty") {
            set_return_type(signature, "bool");
            return true;
        }
        if (method_name == "c_str") {
            set_return_type(signature, "cstr");
            return true;
        }
    }
    if (single_template_type_arg_named(templated_ref, {"list"})) {
        const TypeRef item = first_type_arg_ref(templated_ref).value_or(auto_type_ref());
        if (method_name == "push_back" || method_name == "append") {
            set_param_types(signature, {item});
            set_return_type(signature, "void");
            return true;
        }
        if (method_name == "resize" || method_name == "reserve") {
            set_param_types(signature, {auto_type_ref()});
            set_return_type(signature, "void");
            return true;
        }
        if (method_name == "size" || method_name == "capacity") {
            set_return_type(signature, "usize");
            return true;
        }
        if (method_name == "back" || method_name == "front") {
            set_return_type(signature, item);
            return true;
        }
        if (method_name == "pop_back") {
            set_return_type(signature, "void");
            return true;
        }
        if (method_name == "empty") {
            set_return_type(signature, "bool");
            return true;
        }
        if (method_name == "begin" || method_name == "end") {
            set_return_type(signature, "auto");
            return true;
        }
    }
    if (single_template_type_arg_named(templated_ref, {"set"})) {
        const TypeRef value_type = first_type_arg_ref(templated_ref).value_or(auto_type_ref());
        if (method_name == "contains") {
            set_param_types(signature, {value_type});
            set_return_type(signature, "bool");
            return true;
        }
        if (method_name == "count") {
            set_param_types(signature, {value_type});
            set_return_type(signature, "usize");
            return true;
        }
        if (method_name == "insert") {
            set_param_types(signature, {value_type});
            set_return_type(signature, "auto");
            return true;
        }
        if (method_name == "size") {
            set_return_type(signature, "usize");
            return true;
        }
        if (method_name == "empty") {
            set_return_type(signature, "bool");
            return true;
        }
    }
    if (template_head_is(templated_ref, {"dict"})) {
        const TypeRef key_type = first_type_arg_ref(templated_ref).value_or(auto_type_ref());
        const std::vector<TypeRef> args = template_type_arg_refs(templated_ref, "dict");
        const TypeRef value_type = args.size() == 2 ? args[1] : auto_type_ref();
        if (method_name == "contains") {
            set_param_types(signature, {key_type});
            set_return_type(signature, "bool");
            return true;
        }
        if (method_name == "count") {
            set_param_types(signature, {key_type});
            set_return_type(signature, "usize");
            return true;
        }
        if (method_name == "size") {
            set_return_type(signature, "usize");
            return true;
        }
        if (method_name == "empty") {
            set_return_type(signature, "bool");
            return true;
        }
        if (method_name == "at") {
            set_param_types(signature, {key_type});
            set_return_type(signature, value_type);
            return true;
        }
    }
    if (single_template_type_arg_named(templated_ref, {"Option"})) {
        const TypeRef item = first_type_arg_ref(templated_ref).value_or(auto_type_ref());
        if (method_name == "has_value") {
            set_return_type(signature, "bool");
            return true;
        }
        if (method_name == "value") {
            set_return_type(signature, item);
            return true;
        }
    }
    if (unary_type_child_ref(templated_ref, TypeKind::Atomic)) {
        const TypeRef value_type = first_type_arg_ref(templated_ref).value_or(auto_type_ref());
        if (method_name == "load") {
            set_return_type(signature, value_type);
            return true;
        }
        if (method_name == "store") {
            set_param_types(signature, {value_type});
            set_return_type(signature, "void");
            return true;
        }
        if (method_name == "exchange" || method_name == "fetch_add" || method_name == "fetch_sub" ||
            method_name == "fetch_and" || method_name == "fetch_or" || method_name == "fetch_xor") {
            set_param_types(signature, {value_type});
            set_return_type(signature, value_type);
            return true;
        }
        if (method_name == "is_lock_free") {
            set_return_type(signature, "bool");
            return true;
        }
    }
    return false;
}

} // namespace dudu
