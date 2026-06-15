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

std::string first_type_arg(const std::string& type) {
    if (const auto arg = first_template_type_arg_text(type)) {
        return *arg;
    }
    return first_native_template_arg_text(type).value_or("");
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
    if (templated == "str" || templated == "string" || templated == "string_view" ||
        templated == "std.string" || templated == "std.string_view" || templated == "std::string" ||
        templated == "std::string_view" || templated.find("basic_string") != std::string::npos) {
        if (method_name == "size" || method_name == "length") {
            signature.return_type = "usize";
            return true;
        }
        if (method_name == "empty") {
            signature.return_type = "bool";
            return true;
        }
        if (method_name == "c_str") {
            signature.return_type = "cstr";
            return true;
        }
    }
    if (single_template_type_arg_text(templated, "list") ||
        templated.find("vector<") != std::string::npos) {
        const std::string item = first_type_arg(templated);
        if (method_name == "push_back" || method_name == "append") {
            signature.params = {item.empty() ? "auto" : item};
            signature.return_type = "void";
            return true;
        }
        if (method_name == "resize" || method_name == "reserve") {
            signature.params = {"auto"};
            signature.return_type = "void";
            return true;
        }
        if (method_name == "size" || method_name == "capacity") {
            signature.return_type = "usize";
            return true;
        }
        if (method_name == "empty") {
            signature.return_type = "bool";
            return true;
        }
        if (method_name == "begin" || method_name == "end") {
            signature.return_type = "auto";
            return true;
        }
    }
    if (unary_type_child_text(templated, TypeKind::Atomic) ||
        templated.find("atomic<") != std::string::npos) {
        const std::string item = first_type_arg(templated);
        const std::string value_type = item.empty() ? "auto" : item;
        if (method_name == "load") {
            signature.return_type = value_type;
            return true;
        }
        if (method_name == "store") {
            signature.params = {value_type};
            signature.return_type = "void";
            return true;
        }
        if (method_name == "exchange" || method_name == "fetch_add" || method_name == "fetch_sub" ||
            method_name == "fetch_and" || method_name == "fetch_or" || method_name == "fetch_xor") {
            signature.params = {value_type};
            signature.return_type = value_type;
            return true;
        }
        if (method_name == "is_lock_free") {
            signature.return_type = "bool";
            return true;
        }
    }
    return false;
}

} // namespace dudu
