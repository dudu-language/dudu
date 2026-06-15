#include "dudu/cpp_expr_call_emit.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/decorators.hpp"
#include "dudu/sema_enum.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {

namespace {

bool has_named_argument_shape(const std::vector<Expr>& args) {
    for (const Expr& arg : args) {
        if (arg.kind == ExprKind::NamedArg) {
            return true;
        }
    }
    return false;
}

bool is_builtin_cast_call(std::string_view name) {
    static const std::vector<std::string_view> types = {"bool", "i8",  "i16", "i32", "i64",
                                                        "u8",   "u16", "u32", "u64", "usize",
                                                        "f32",  "f64", "str", "cstr"};
    return std::find(types.begin(), types.end(), name) != types.end();
}

bool is_pointer_cast_type_like(const std::string& type) {
    const std::string trimmed = trim_copy(type);
    if (is_builtin_cast_call(trimmed) || starts_with(trimmed, "struct ")) {
        return true;
    }
    const TypeRef parsed = parse_type_text(trimmed);
    switch (parsed.kind) {
    case TypeKind::Template:
    case TypeKind::Qualified:
    case TypeKind::FixedArray:
    case TypeKind::Const:
    case TypeKind::Volatile:
    case TypeKind::Atomic:
    case TypeKind::Storage:
    case TypeKind::Shared:
    case TypeKind::Device:
    case TypeKind::Static:
    case TypeKind::Function:
        return true;
    case TypeKind::Named:
        return !parsed.name.empty() &&
               std::isupper(static_cast<unsigned char>(parsed.name.front())) != 0;
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Value:
    case TypeKind::Unknown:
        return false;
    }
    return false;
}

std::string lower_call_args_for_signature(const std::vector<Expr>& args, const FunctionSignature&,
                                          const std::vector<std::string>& aliases,
                                          const std::map<std::string, std::string>& locals,
                                          const Symbols* symbols) {
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_expr(args[i], aliases, locals, symbols);
    }
    return out.str();
}

bool is_pointer_type(std::string type) {
    type = trim_copy(std::move(type));
    return parse_type_text(type).kind == TypeKind::Pointer;
}

bool is_pointer_list_type(std::string type) {
    type = trim_copy(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind != TypeKind::Template || parsed.name != "list" || parsed.children.size() != 1) {
        return false;
    }
    return parsed.children.front().kind == TypeKind::Pointer;
}

bool expression_has_pointer_type(const Expr& expr, const std::map<std::string, std::string>& locals,
                                 const Symbols* symbols) {
    if (is_pointer_receiver_expr(expr, locals)) {
        return true;
    }
    if (symbols == nullptr) {
        return false;
    }
    const std::string type = member_expr_type(*symbols, locals, nullptr, expr);
    return is_pointer_type(type);
}

bool is_supported_swizzle(const std::string& swizzle) {
    if (swizzle.size() < 2 || swizzle.size() > 4) {
        return false;
    }
    for (const std::string_view set :
         {std::string_view("xyzw"), std::string_view("rgba"), std::string_view("stpq")}) {
        bool matches = true;
        for (const char ch : swizzle) {
            if (set.find(ch) == std::string_view::npos) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

std::optional<std::string_view> swizzle_component_order(const std::string& swizzle) {
    if (!is_supported_swizzle(swizzle)) {
        return std::nullopt;
    }
    for (const std::string_view set :
         {std::string_view("xyzw"), std::string_view("rgba"), std::string_view("stpq")}) {
        if (set.find(swizzle.front()) != std::string_view::npos) {
            return set.substr(0, swizzle.size());
        }
    }
    return std::nullopt;
}

bool looks_like_local_dudu_class_type(const std::string& type) {
    const std::string trimmed = trim_copy(type);
    return !trimmed.empty() && trimmed.find('.') == std::string::npos &&
           trimmed.find("::") == std::string::npos &&
           std::isupper(static_cast<unsigned char>(trimmed.front())) != 0;
}

std::optional<std::string>
lower_local_swizzle_expr(const Expr& expr, const std::vector<std::string>& aliases,
                         const std::map<std::string, std::string>& locals) {
    if (expr.kind != ExprKind::Member || expr.children.size() != 1 ||
        expr.children.front().kind != ExprKind::Name || !is_supported_swizzle(expr.name)) {
        return std::nullopt;
    }
    const std::string& receiver = expr.children.front().name;
    const auto local = locals.find(receiver);
    if (local == locals.end()) {
        return std::nullopt;
    }
    if (!looks_like_local_dudu_class_type(local->second)) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << lower_cpp_type(local->second, aliases) << "{";
    for (size_t i = 0; i < expr.name.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << receiver << "." << expr.name[i];
    }
    out << "}";
    return out.str();
}

std::string unquoted_string_literal(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string decorator_arg(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (const std::optional<std::string> arg = decorator_first_arg_text(decorator, name)) {
            return *arg;
        }
    }
    return {};
}

std::string unquoted(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::optional<std::string> dudu_operator_method_name(const Symbols& symbols, std::string type,
                                                     std::string_view op) {
    type = trim_copy(resolve_alias(symbols, std::move(type)));
    while (true) {
        const TypeRef parsed = parse_type_text(type);
        if (const auto inner = unary_type_child_text(
                parsed, {TypeKind::Const, TypeKind::Pointer, TypeKind::Reference})) {
            type = *inner;
            continue;
        }
        break;
    }
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (unquoted(decorator_arg(method, "operator")) == op) {
            return method.name;
        }
    }
    return std::nullopt;
}

std::vector<Expr> index_arg_exprs(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        return index_expr.children;
    }
    return {index_expr};
}

bool is_full_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           expr.children[0].text.empty() && expr.children[1].text.empty();
}

std::string lower_named_argument_call(const Expr& expr, const std::vector<std::string>& aliases,
                                      const std::map<std::string, std::string>& locals,
                                      const Symbols* symbols) {
    std::ostringstream out;
    out << lower_callee_expr(expr, aliases, locals, symbols) << "{";
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (expr.children[i].kind == ExprKind::NamedArg && expr.children[i].children.size() == 1) {
            out << "." << expr.children[i].name << " = "
                << lower_expr(expr.children[i].children.front(), aliases, locals, symbols);
            continue;
        }
        out << lower_expr(expr.children[i], aliases, locals, symbols);
    }
    out << "}";
    return out.str();
}

} // namespace

bool is_builtin_template_constructor(std::string_view name) {
    static const std::vector<std::string_view> types = {"list", "dict", "set", "atomic", "span"};
    return std::find(types.begin(), types.end(), name) != types.end();
}

std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const std::map<std::string, std::string>& locals,
                              const Symbols* symbols) {
    if (!expr.callee.empty()) {
        const Expr& callee = expr.callee.front();
        if (callee.kind == ExprKind::Member && callee.children.size() == 1 &&
            callee.children.front().kind == ExprKind::Name &&
            callee.children.front().name == "super") {
            if (const auto base = locals.find("super"); base != locals.end()) {
                return base->second + "::" + callee.name;
            }
        }
        if (callee.kind == ExprKind::Member && callee.children.size() == 1 &&
            expression_has_pointer_type(callee.children.front(), locals, symbols)) {
            return lower_expr(callee.children.front(), aliases, locals, symbols) + "->" +
                   callee.name;
        }
    }
    if (!expr.callee.empty()) {
        return lower_expr(expr.callee.front(), aliases, locals, symbols);
    }
    return expr.name;
}

bool is_pointer_receiver_expr(const Expr& expr, const std::map<std::string, std::string>& locals) {
    if (expr.kind == ExprKind::Name) {
        const auto local = locals.find(expr.name);
        return local != locals.end() && is_pointer_type(local->second);
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2 &&
        expr.children.front().kind == ExprKind::Name) {
        const auto local = locals.find(expr.children.front().name);
        return local != locals.end() && is_pointer_list_type(local->second);
    }
    return false;
}

std::optional<std::string> lower_swizzle_expr(const Expr& expr,
                                              const std::vector<std::string>& aliases,
                                              const std::map<std::string, std::string>& locals,
                                              const Symbols* symbols) {
    if (const auto local = lower_local_swizzle_expr(expr, aliases, locals)) {
        return local;
    }
    if (expr.kind != ExprKind::Member || expr.children.size() != 1 ||
        !is_supported_swizzle(expr.name)) {
        return std::nullopt;
    }
    if (expr.children.front().kind == ExprKind::Name &&
        locals.contains(expr.children.front().name)) {
        return std::nullopt;
    }
    const std::string receiver = lower_expr(expr.children.front(), aliases, locals, symbols);
    if (receiver.empty()) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "([&]() { auto&& __dudu_swizzle_value = " << receiver
        << "; return std::remove_cvref_t<decltype(__dudu_swizzle_value)>{";
    for (size_t i = 0; i < expr.name.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << "__dudu_swizzle_value." << expr.name[i];
    }
    out << "}; }())";
    return out.str();
}

std::optional<std::string>
lower_swizzle_assignment(const Stmt& stmt, const std::vector<std::string>& aliases,
                         const std::map<std::string, std::string>& locals, const Symbols* symbols) {
    if (stmt.target_expr.kind != ExprKind::Member || stmt.target_expr.children.size() != 1) {
        return std::nullopt;
    }
    const Expr& receiver_expr = stmt.target_expr.children.front();
    if (!is_supported_swizzle(stmt.target_expr.name)) {
        return std::nullopt;
    }
    const auto rhs_order = swizzle_component_order(stmt.target_expr.name);
    if (!rhs_order) {
        return std::nullopt;
    }
    std::string receiver = lower_expr(receiver_expr, aliases, locals, symbols);
    if (receiver.empty()) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "([&]() { auto&& __dudu_swizzle_rhs = "
        << lower_expr(stmt.value_expr, aliases, locals, symbols) << "; ";
    for (size_t i = 0; i < stmt.target_expr.name.size(); ++i) {
        out << receiver << "." << stmt.target_expr.name[i] << " = __dudu_swizzle_rhs."
            << (*rhs_order)[i] << "; ";
    }
    out << "}())";
    return out.str();
}

bool enum_has_payloads(const EnumDecl& en) {
    for (const EnumValueDecl& value : en.values) {
        if (!value.payload_fields.empty()) {
            return true;
        }
    }
    return false;
}

std::string lower_enum_variant_constructor(const EnumDecl& en, const EnumValueDecl& value,
                                           const std::vector<Expr>& args,
                                           const std::vector<std::string>& aliases,
                                           const std::map<std::string, std::string>& locals,
                                           const Symbols* symbols) {
    std::ostringstream out;
    out << en.name << "{" << en.name << "::" << value.name << "{";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (args[i].kind == ExprKind::NamedArg && args[i].children.size() == 1) {
            out << "." << args[i].name << " = "
                << lower_expr(args[i].children.front(), aliases, locals, symbols);
        } else {
            out << lower_expr(args[i], aliases, locals, symbols);
        }
    }
    out << "}}";
    return out.str();
}

std::optional<std::string> lower_trailing_full_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const std::map<std::string, std::string>& locals, const Symbols* symbols) {
    if (index.kind != ExprKind::TupleLiteral || index.children.empty() ||
        !is_full_slice_expr(index.children.back())) {
        return std::nullopt;
    }
    std::string row = lower_expr(base, aliases, locals, symbols);
    for (size_t i = 0; i + 1 < index.children.size(); ++i) {
        if (index.children[i].kind == ExprKind::Slice) {
            return std::nullopt;
        }
        row += "[" + lower_expr(index.children[i], aliases, locals, symbols) + "]";
    }
    return "std::span(&(" + row + ")[0], (" + row + ").size())";
}

std::optional<std::string>
lower_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const Symbols* symbols) {
    if (symbols == nullptr || stmt.target_expr.kind != ExprKind::Index ||
        stmt.target_expr.children.size() != 2 ||
        stmt.target_expr.children[0].kind != ExprKind::Name) {
        return std::nullopt;
    }
    const std::string& receiver = stmt.target_expr.children[0].name;
    const auto local = locals.find(receiver);
    if (local == locals.end()) {
        return std::nullopt;
    }
    const auto method = dudu_operator_method_name(*symbols, local->second, "[]=");
    if (!method) {
        return std::nullopt;
    }
    std::vector<Expr> args = index_arg_exprs(stmt.target_expr.children[1]);
    args.push_back(stmt.value_expr);
    return receiver + "." + *method + "(" +
           join_lowered_exprs(args, aliases, locals, ", ", symbols) + ")";
}

std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const std::map<std::string, std::string>& locals,
                                 const Symbols* symbols) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        return expr.name;
    }
    if (expr.kind == ExprKind::StringLiteral) {
        return unquoted_string_literal(expr.text);
    }
    if (expr.kind == ExprKind::Member) {
        if (const std::optional<std::string> path = member_path_from_expr(expr)) {
            return *path;
        }
    }
    return lower_expr(expr, aliases, locals, symbols);
}

std::optional<std::string>
lower_pointer_cast_expr(const Expr& expr, const std::vector<std::string>& aliases,
                        const std::map<std::string, std::string>& locals, const Symbols* symbols) {
    if (expr.op != "*" || expr.children.size() != 1 ||
        expr.children.front().kind != ExprKind::Call) {
        return std::nullopt;
    }
    const Expr& call = expr.children.front();
    const std::string type_name = call_callee_text(call);
    if (!is_pointer_cast_type_like(type_name)) {
        return std::nullopt;
    }
    return "reinterpret_cast<" + lower_cpp_pointer_type(type_name, aliases) + ">(" +
           join_lowered_exprs(call.children, aliases, locals, ", ", symbols) + ")";
}

std::string lower_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const Symbols* symbols) {
    if (!expr.callee.empty()) {
        if (symbols != nullptr) {
            if (const auto variant = enum_variant_from_expr(*symbols, expr.callee.front())) {
                if (enum_has_payloads(*variant->first)) {
                    return lower_enum_variant_constructor(*variant->first, *variant->second,
                                                          expr.children, aliases, locals, symbols);
                }
            }
        }
    }
    if (has_named_argument_shape(expr.children)) {
        return lower_named_argument_call(expr, aliases, locals, symbols);
    }
    const std::string callee_name = call_callee_text(expr);
    if (starts_with(callee_name, "*")) {
        const std::string type = trim_copy(callee_name.substr(1));
        if (is_pointer_cast_type_like(type)) {
            return "reinterpret_cast<" + lower_cpp_pointer_type(type, aliases) + ">(" +
                   join_lowered_exprs(expr.children, aliases, locals, ", ", symbols) + ")";
        }
    }
    if (expr.name == "len" && expr.children.size() == 1) {
        return "(" + lower_expr(expr.children.front(), aliases, locals, symbols) + ").size()";
    }
    if (expr.name == "str" && expr.children.size() == 1) {
        if (expr.children.front().kind == ExprKind::StringLiteral) {
            return "std::string(" + lower_expr(expr.children.front(), aliases, locals, symbols) +
                   ")";
        }
        return "std::to_string(" + lower_expr(expr.children.front(), aliases, locals, symbols) +
               ")";
    }
    if ((expr.name == "Ok" || expr.name == "Err") && expr.children.size() == 1) {
        return "dudu::" + expr.name + "(" +
               join_lowered_exprs(expr.children, aliases, locals, ", ", symbols) + ")";
    }
    if (is_builtin_cast_call(expr.name)) {
        return lower_cpp_type(expr.name, aliases) + "(" +
               join_lowered_exprs(expr.children, aliases, locals, ", ", symbols) + ")";
    }
    std::string callee = lower_callee_expr(expr, aliases, locals, symbols);
    if (ends_with(callee, ".append")) {
        callee.replace(callee.size() - 7, 7, ".push_back");
    }
    if (symbols != nullptr) {
        if (const auto signature = symbols->function_signatures.find(callee_name);
            signature != symbols->function_signatures.end()) {
            return callee + "(" +
                   lower_call_args_for_signature(expr.children, signature->second, aliases, locals,
                                                 symbols) +
                   ")";
        }
    }
    return callee + "(" + join_lowered_exprs(expr.children, aliases, locals, ", ", symbols) + ")";
}

} // namespace dudu
