#include "dudu/sema_builtin_methods.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_function_type.hpp"

#include <optional>
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

bool single_template_type_arg(const TypeRef& type, std::string_view name) {
    return template_type_arg_refs(type, name).size() == 1;
}

void set_return_type(FunctionSignature& signature, const std::string& type) {
    set_signature_return_type(signature, parse_type_text(type));
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
    while (true) {
        if (const auto inner =
                unary_type_child_ref(type, {TypeKind::Pointer, TypeKind::Reference})) {
            type = *inner;
            type = resolve_alias_ref(symbols, std::move(type));
            continue;
        }
        break;
    }
    return type;
}

bool builtin_cpp_method_signature(const Symbols& symbols, const TypeRef& receiver_type,
                                  const std::string& method_name, FunctionSignature& signature) {
    TypeRef templated_ref = receiver_template_type_ref(symbols, receiver_type);
    std::string templated = substitute_type_ref_text(templated_ref, {});
    if (templated == "str" || templated == "string" || templated == "string_view" ||
        templated == "std.string" || templated == "std.string_view" || templated == "std::string" ||
        templated == "std::string_view" || templated.find("basic_string") != std::string::npos) {
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
    if (single_template_type_arg(templated_ref, "list") ||
        templated.find("vector<") != std::string::npos) {
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
    if (single_template_type_arg(templated_ref, "set") ||
        templated.find("unordered_set<") != std::string::npos ||
        templated.find("std::set<") != std::string::npos || templated.find("set<") == 0) {
        const TypeRef value_type = first_type_arg_ref(templated_ref).value_or(auto_type_ref());
        if (method_name == "contains") {
            set_param_types(signature, {value_type});
            set_return_type(signature, "bool");
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
    if ((templated_ref.kind == TypeKind::Template && templated_ref.name == "dict") ||
        templated.find("unordered_map<") != std::string::npos ||
        templated.find("std::map<") != std::string::npos || templated.find("map<") == 0) {
        const TypeRef key_type = first_type_arg_ref(templated_ref).value_or(auto_type_ref());
        if (method_name == "contains") {
            set_param_types(signature, {key_type});
            set_return_type(signature, "bool");
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
    if (single_template_type_arg(templated_ref, "Option") ||
        single_template_type_arg(templated_ref, "std.optional") ||
        templated.find("optional<") != std::string::npos) {
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
    if (unary_type_child_ref(templated_ref, TypeKind::Atomic) ||
        templated.find("atomic<") != std::string::npos) {
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
