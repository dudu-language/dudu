#include "dudu/sema_builtin_methods.hpp"

#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/source.hpp"

namespace dudu {
namespace {

std::optional<std::string> first_native_template_arg_text(const std::string& type) {
    const size_t open = type.find('<');
    if (open == std::string::npos || type.empty() || type.back() != '>') {
        return std::nullopt;
    }

    int angle_depth = 0;
    int bracket_depth = 0;
    int paren_depth = 0;
    const size_t close = type.size() - 1;
    for (size_t i = open + 1; i < close; ++i) {
        const char c = type[i];
        if (c == '<') {
            ++angle_depth;
        } else if (c == '>') {
            --angle_depth;
        } else if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            --bracket_depth;
        } else if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            --paren_depth;
        } else if (c == ',' && angle_depth == 0 && bracket_depth == 0 && paren_depth == 0) {
            return trim_copy(type.substr(open + 1, i - open - 1));
        }
    }
    return trim_copy(type.substr(open + 1, close - open - 1));
}

std::string first_type_arg(const TypeRef& type) {
    if (const auto arg =
            unary_type_child_text(type, {TypeKind::Atomic, TypeKind::Const, TypeKind::Volatile,
                                         TypeKind::Device, TypeKind::Storage, TypeKind::Shared})) {
        return *arg;
    }
    if (const auto arg = first_template_type_arg_text(type)) {
        return *arg;
    }
    return first_native_template_arg_text(type.text).value_or("");
}

bool single_template_type_arg(const TypeRef& type, std::string_view name) {
    return single_template_type_arg_text(type, name).has_value();
}

void set_return_type(FunctionSignature& signature, const std::string& type) {
    signature.return_type = type;
    signature.return_type_ref = parse_type_text(type);
}

void set_param_types(FunctionSignature& signature, std::initializer_list<std::string> types) {
    signature.params.assign(types.begin(), types.end());
    signature.param_type_refs.clear();
    signature.param_type_refs.reserve(signature.params.size());
    for (const std::string& type : signature.params) {
        signature.param_type_refs.push_back(parse_type_text(type));
    }
}

} // namespace

std::string receiver_template_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        const TypeRef parsed = parse_type_text(type);
        if (const auto inner =
                unary_type_child_text(parsed, {TypeKind::Pointer, TypeKind::Reference})) {
            type = *inner;
            continue;
        }
        break;
    }
    return trim(std::move(type));
}

bool builtin_cpp_method_signature(const Symbols& symbols, std::string receiver_type,
                                  const std::string& method_name, FunctionSignature& signature) {
    const std::string templated = receiver_template_type(symbols, std::move(receiver_type));
    const TypeRef templated_ref = parse_type_text(templated);
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
        const std::string item = first_type_arg(templated_ref);
        if (method_name == "push_back" || method_name == "append") {
            set_param_types(signature, {item.empty() ? "auto" : item});
            set_return_type(signature, "void");
            return true;
        }
        if (method_name == "resize" || method_name == "reserve") {
            set_param_types(signature, {"auto"});
            set_return_type(signature, "void");
            return true;
        }
        if (method_name == "size" || method_name == "capacity") {
            set_return_type(signature, "usize");
            return true;
        }
        if (method_name == "back" || method_name == "front") {
            set_return_type(signature, item.empty() ? "auto" : item);
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
        const std::string item = first_type_arg(templated_ref);
        const std::string value_type = item.empty() ? "auto" : item;
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
        const std::string key_type = first_type_arg(templated_ref);
        if (method_name == "contains") {
            set_param_types(signature, {key_type.empty() ? "auto" : key_type});
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
        const std::string item = first_type_arg(templated_ref);
        if (method_name == "has_value") {
            set_return_type(signature, "bool");
            return true;
        }
        if (method_name == "value") {
            set_return_type(signature, item.empty() ? "auto" : item);
            return true;
        }
    }
    if (unary_type_child_text(templated_ref, TypeKind::Atomic) ||
        templated.find("atomic<") != std::string::npos) {
        const std::string item = first_type_arg(templated_ref);
        const std::string value_type = item.empty() ? "auto" : item;
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
