#include "dudu/sema_ops.hpp"

#include "dudu/type_compat.hpp"

#include <map>
#include <set>

namespace dudu {
namespace {

bool is_integer_type(std::string type) {
    type = trim(std::move(type));
    static const std::set<std::string> integers = {"i8",  "i16", "i32", "i64",   "u8",
                                                   "u16", "u32", "u64", "usize", "isize"};
    return integers.contains(type);
}

bool is_numeric_type(std::string type) {
    type = trim(std::move(type));
    static const std::set<std::string> numeric = {"i8",  "i16", "i32", "i64", "u8",    "u16",
                                                  "u32", "u64", "f32", "f64", "usize", "isize"};
    return numeric.contains(type);
}

std::string unwrap_value_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        type = trim(std::move(type));
        bool unwrapped = false;
        for (const char* wrapper : {"const", "volatile", "storage", "shared", "device"}) {
            const std::string prefix = std::string(wrapper) + "[";
            if (type.rfind(prefix, 0) == 0 && type.back() == ']') {
                type = trim(type.substr(prefix.size(), type.size() - prefix.size() - 1));
                unwrapped = true;
                break;
            }
        }
        if (!unwrapped) {
            return type;
        }
    }
}

bool unknown_or_auto(const std::string& type) {
    const std::string text = trim(type);
    return text.empty() || text == "auto" || text == "reference" || text == "const_reference" ||
           text == "iterator" || text == "const_iterator" || text.ends_with(".reference") ||
           text.ends_with(".const_reference") || text.ends_with(".iterator") ||
           text.ends_with(".const_iterator");
}

bool same_foreign_cpp_type(const std::string& left, const std::string& right) {
    const std::string lhs = trim(left);
    return !lhs.empty() && lhs == trim(right) && lhs.find('.') != std::string::npos;
}

bool numeric_operand_allowed(const std::string& expected, const std::string& expr,
                             const std::string& got) {
    return is_numeric_type(expected) && assignment_type_allowed(expected, expr, got);
}

bool same_or_assignable(const std::string& left, const std::string& right_expr,
                        const std::string& right) {
    return assignment_type_allowed(left, right_expr, right) ||
           assignment_type_allowed(right, "", left);
}

std::string operator_method_name(const std::string& op) {
    static const std::map<std::string, std::string> names = {
        {"+", "__add__"},      {"-", "__sub__"}, {"*", "__mul__"}, {"/", "__truediv__"},
        {"%", "__mod__"},      {"==", "__eq__"}, {"!=", "__ne__"}, {"<", "__lt__"},
        {"<=", "__le__"},      {">", "__gt__"},  {">=", "__ge__"}, {"bool", "__bool__"},
        {"[]", "__getitem__"},
    };
    const auto it = names.find(op);
    return it == names.end() ? "" : it->second;
}

std::string unquoted(std::string text) {
    text = trim(std::move(text));
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

bool method_has_operator(const FunctionDecl& method, const std::string& op) {
    for (const Decorator& decorator : method.decorators) {
        const std::string text = trim(decorator.text);
        const std::string prefix = "operator(";
        if (text.starts_with(prefix) && text.ends_with(")") &&
            unquoted(text.substr(prefix.size(), text.size() - prefix.size() - 1)) == op) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<FunctionSignature>
dudu_operator_signature(const Symbols& symbols, const std::string& op, const std::string& left) {
    const std::string method_name = operator_method_name(op);
    if (method_name.empty()) {
        return std::nullopt;
    }
    const auto klass = symbols.classes.find(unwrap_value_type(symbols, left));
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name && !method_has_operator(method, op)) {
            continue;
        }
        FunctionSignature signature;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            signature.params.push_back(method.params[i].type);
        }
        signature.return_type = method.return_type.empty() ? "void" : method.return_type;
        return signature;
    }
    return std::nullopt;
}

bool binary_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                        const std::string& right_expr, const std::string& right) {
    const std::string resolved_left = resolve_alias(symbols, left);
    const std::string resolved_right = resolve_alias(symbols, right);
    const std::string value_left = unwrap_value_type(symbols, left);
    const std::string value_right = unwrap_value_type(symbols, right);
    if (unknown_or_auto(left) || unknown_or_auto(right) || unknown_or_auto(value_left) ||
        unknown_or_auto(value_right)) {
        return true;
    }
    if (same_foreign_cpp_type(value_left, value_right)) {
        return true;
    }
    if (op == "+" && value_left == "str") {
        return assignment_type_allowed(value_left, right_expr, value_right);
    }
    if (op == "+" || op == "-") {
        return (!trim(resolved_left).empty() && trim(resolved_left).front() == '*' &&
                is_integer_type(value_right)) ||
               numeric_operand_allowed(value_left, right_expr, value_right);
    }
    if (op == "*" || op == "/") {
        return numeric_operand_allowed(value_left, right_expr, value_right);
    }
    if (op == "%" || op == "^" || op == "&" || op == "|" || op == "<<" || op == ">>") {
        return is_integer_type(value_left) &&
               (assignment_type_allowed(value_left, right_expr, value_right) ||
                is_integer_type(value_right));
    }
    return false;
}

bool comparison_rhs_allowed(const Symbols& symbols, const std::string& op, const std::string& left,
                            const std::string& right_expr, const std::string& right) {
    const std::string value_left = unwrap_value_type(symbols, left);
    const std::string value_right = unwrap_value_type(symbols, right);
    if (unknown_or_auto(left) || unknown_or_auto(right) || unknown_or_auto(value_left) ||
        unknown_or_auto(value_right)) {
        return true;
    }
    if (same_foreign_cpp_type(value_left, value_right)) {
        return true;
    }
    if (op == "==" || op == "!=") {
        return same_or_assignable(value_left, right_expr, value_right);
    }
    if (value_left == "str") {
        return assignment_type_allowed(value_left, right_expr, value_right);
    }
    return numeric_operand_allowed(value_left, right_expr, value_right);
}

} // namespace dudu
