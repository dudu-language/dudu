#include "dudu/cpp_stmt_emit.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_pointer_members.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

std::string indent(int depth) {
    return std::string(static_cast<size_t>(depth) * 4, ' ');
}

std::string lower_cpp_escape_expr(std::string expr, const std::vector<std::string>& aliases,
                                  const std::map<std::string, std::string>& locals) {
    return lower_raw_cpp_escape_expr(rewrite_pointer_members(std::move(expr), locals), aliases);
}

std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals,
                       const Symbols* symbols = nullptr);

std::string lower_name_expr(const std::string& name) {
    return name;
}

std::string lower_member_expr(std::string receiver, const std::string& member,
                              const std::vector<std::string>& aliases) {
    receiver = trim_copy(std::move(receiver));
    if (receiver.empty()) {
        return member;
    }
    const std::string dotted = receiver + "." + member;
    const std::string qualified = qualify_namespace_aliases(dotted, aliases);
    if (qualified != dotted) {
        return qualified;
    }
    const size_t head_end = receiver.find_first_of(".:");
    const std::string head =
        head_end == std::string::npos ? receiver : receiver.substr(0, head_end);
    const bool scoped_receiver =
        receiver.find("::") != std::string::npos ||
        (!head.empty() && std::isupper(static_cast<unsigned char>(head.front())) != 0);
    return receiver + (scoped_receiver ? "::" : ".") + member;
}

std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals,
                               std::string_view separator = ", ",
                               const Symbols* symbols = nullptr) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << lower_expr(exprs[i], aliases, locals, symbols);
    }
    return out.str();
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

std::string join_names(const std::vector<std::string>& names) {
    std::ostringstream out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << names[i];
    }
    return out.str();
}

bool has_expr(const Expr& expr) {
    return !expr.text.empty();
}

std::string join_lowered_template_args(const std::vector<Expr>& exprs,
                                       const std::vector<std::string>& aliases) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_template_call_arg(exprs[i].text, aliases);
    }
    return out.str();
}

std::string join_lowered_type_args(const std::vector<TypeRef>& types,
                                   const std::vector<std::string>& aliases) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(types[i], aliases);
    }
    return out.str();
}

std::string join_type_arg_texts(const std::vector<TypeRef>& types) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << types[i].text;
    }
    return out.str();
}

std::string join_template_arg_texts(const std::vector<Expr>& exprs) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << exprs[i].text;
    }
    return out.str();
}

std::string cpp_binary_operator(const std::string& op) {
    if (op == "and") {
        return "&&";
    }
    if (op == "or") {
        return "||";
    }
    return op;
}

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
    const std::string base =
        type.find('[') == std::string::npos ? type : type.substr(0, type.find('['));
    return is_builtin_cast_call(type) || starts_with(type, "struct ") ||
           type.find('[') != std::string::npos || type.find('.') != std::string::npos ||
           (!base.empty() && std::isupper(static_cast<unsigned char>(base.front())) != 0);
}

std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const std::map<std::string, std::string>& locals) {
    if (!expr.callee.empty()) {
        const Expr& callee = expr.callee.front();
        if (callee.kind == ExprKind::Member && callee.children.size() == 1 &&
            callee.children.front().kind == ExprKind::Name &&
            callee.children.front().name == "super") {
            if (const auto base = locals.find("super"); base != locals.end()) {
                return base->second + "::" + callee.name;
            }
        }
    }
    if (!expr.callee.empty()) {
        return lower_expr(expr.callee.front(), aliases, locals);
    }
    return lower_name_expr(expr.name);
}

bool is_pointer_type(std::string type) {
    type = trim_copy(std::move(type));
    return !type.empty() && type.front() == '*';
}

bool is_pointer_list_type(std::string type) {
    type = trim_copy(std::move(type));
    return starts_with(type, "list[*") && ends_with(type, "]");
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

std::optional<std::string> lower_swizzle_expr(const Expr& expr,
                                              const std::vector<std::string>& aliases,
                                              const std::map<std::string, std::string>& locals) {
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
    const std::string receiver = lower_expr(expr.children.front(), aliases, locals);
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

std::string unquoted_string_literal(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string decorator_arg(const FunctionDecl& fn, std::string_view name) {
    const std::string prefix = std::string(name) + "(";
    for (const Decorator& decorator : fn.decorators) {
        const std::string text = trim_copy(decorator.text);
        if (starts_with(text, prefix) && ends_with(text, ")")) {
            return trim_copy(text.substr(prefix.size(), text.size() - prefix.size() - 1));
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
    if (starts_with(type, "const[") && ends_with(type, "]")) {
        type = trim_copy(type.substr(6, type.size() - 7));
    }
    if (!type.empty() && (type.front() == '&' || type.front() == '*')) {
        type = trim_copy(type.substr(1));
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

bool enum_has_payloads(const EnumDecl& en) {
    for (const EnumValueDecl& value : en.values) {
        if (!value.payload_fields.empty()) {
            return true;
        }
    }
    return false;
}

const EnumDecl* enum_decl_for_type(const Symbols* symbols, const std::string& type) {
    if (symbols == nullptr) {
        return nullptr;
    }
    const auto found = symbols->enums.find(type);
    return found == symbols->enums.end() ? nullptr : found->second;
}

const EnumValueDecl* enum_variant_decl(const EnumDecl& en, const std::string& variant) {
    for (const EnumValueDecl& value : en.values) {
        if (value.name == variant) {
            return &value;
        }
    }
    return nullptr;
}

std::optional<std::pair<const EnumDecl*, const EnumValueDecl*>>
enum_variant_from_path(const Symbols* symbols, const std::string& path) {
    if (symbols == nullptr) {
        return std::nullopt;
    }
    const size_t dot = path.find('.');
    if (dot == std::string::npos || path.find('.', dot + 1) != std::string::npos) {
        return std::nullopt;
    }
    const auto en = symbols->enums.find(path.substr(0, dot));
    if (en == symbols->enums.end()) {
        return std::nullopt;
    }
    const EnumValueDecl* value = enum_variant_decl(*en->second, path.substr(dot + 1));
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::make_pair(en->second, value);
}

std::optional<std::pair<const EnumDecl*, const EnumValueDecl*>>
enum_variant_from_expr(const Symbols* symbols, const Expr& expr) {
    if (const std::optional<std::string> path = member_path_from_expr(expr)) {
        return enum_variant_from_path(symbols, *path);
    }
    return std::nullopt;
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
    return receiver + "." + *method + "(" + join_lowered_exprs(args, aliases, locals) + ")";
}

std::string lower_offsetof_field(const Expr& expr, const std::vector<std::string>& aliases,
                                 const std::map<std::string, std::string>& locals) {
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
    return lower_expr(expr, aliases, locals);
}

std::optional<std::string>
lower_pointer_cast_expr(const Expr& expr, const std::vector<std::string>& aliases,
                        const std::map<std::string, std::string>& locals) {
    if (expr.op != "*" || expr.children.size() != 1 ||
        expr.children.front().kind != ExprKind::Call) {
        return std::nullopt;
    }
    const Expr& call = expr.children.front();
    const std::string type_name = call_callee_text(call);
    if (!is_pointer_cast_type_like(type_name)) {
        return std::nullopt;
    }
    return "reinterpret_cast<" + lower_cpp_type("*" + type_name, aliases) + ">(" +
           join_lowered_exprs(call.children, aliases, locals) + ")";
}

std::string lower_named_argument_call(const Expr& expr, const std::vector<std::string>& aliases,
                                      const std::map<std::string, std::string>& locals) {
    std::ostringstream out;
    out << lower_callee_expr(expr, aliases, locals) << "{";
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (expr.children[i].kind == ExprKind::NamedArg && expr.children[i].children.size() == 1) {
            out << "." << expr.children[i].name << " = "
                << lower_expr(expr.children[i].children.front(), aliases, locals);
            continue;
        }
        out << lower_expr(expr.children[i], aliases, locals);
    }
    out << "}";
    return out.str();
}

std::string lower_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                            const std::map<std::string, std::string>& locals,
                            const Symbols* symbols) {
    if (!expr.callee.empty()) {
        if (const auto variant = enum_variant_from_expr(symbols, expr.callee.front())) {
            if (enum_has_payloads(*variant->first)) {
                return lower_enum_variant_constructor(*variant->first, *variant->second,
                                                      expr.children, aliases, locals, symbols);
            }
        }
    }
    if (has_named_argument_shape(expr.children)) {
        return lower_named_argument_call(expr, aliases, locals);
    }
    const std::string callee_name = call_callee_text(expr);
    if (starts_with(callee_name, "*")) {
        const std::string type = trim_copy(callee_name.substr(1));
        if (is_pointer_cast_type_like(type)) {
            return "reinterpret_cast<" + lower_cpp_type("*" + type, aliases) + ">(" +
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
    if (is_builtin_cast_call(expr.name)) {
        return lower_cpp_type(expr.name, aliases) + "(" +
               join_lowered_exprs(expr.children, aliases, locals, ", ", symbols) + ")";
    }
    std::string callee = lower_callee_expr(expr, aliases, locals);
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

std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals, const Symbols* symbols) {
    if (expr.text.empty()) {
        return {};
    }
    if (expr.kind == ExprKind::Unknown) {
        throw CompileError(expr.location, "unsupported expression: " + trim_copy(expr.text));
    }
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
        return expr.text == "True" ? "true" : "false";
    case ExprKind::NoneLiteral:
        return "nullptr";
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
        return lower_numeric_separators(expr.text);
    case ExprKind::Name:
        if (expr.name == "class") {
            if (const auto current_class = locals.find("class"); current_class != locals.end()) {
                return current_class->second;
            }
        }
        return lower_name_expr(expr.name);
    case ExprKind::CppEscape:
        return lower_cpp_escape_expr(cpp_escape_body(expr.text), aliases, locals);
    case ExprKind::StringLiteral:
        return expr.text;
    case ExprKind::Unary:
        if (const auto pointer_cast = lower_pointer_cast_expr(expr, aliases, locals)) {
            return *pointer_cast;
        }
        if (expr.children.size() == 1) {
            const std::string op = expr.op == "not" ? "!" : expr.op;
            return "(" + op + lower_expr(expr.children.front(), aliases, locals, symbols) + ")";
        }
        break;
    case ExprKind::Binary:
        if (expr.children.size() == 2 && has_expr(expr.children[0]) && has_expr(expr.children[1])) {
            return "(" + lower_expr(expr.children[0], aliases, locals, symbols) + " " +
                   cpp_binary_operator(expr.op) + " " +
                   lower_expr(expr.children[1], aliases, locals, symbols) + ")";
        }
        break;
    case ExprKind::Conditional:
        throw CompileError(expr.location,
                           "unsupported Python feature: conditional expressions; use an "
                           "explicit if statement");
    case ExprKind::Await:
        throw CompileError(expr.location, "unsupported Python feature: async");
    case ExprKind::Yield:
        throw CompileError(expr.location, "unsupported Python feature: generators");
    case ExprKind::Call:
        return lower_call_expr(expr, aliases, locals, symbols);
    case ExprKind::TemplateCall: {
        if (expr.template_args.empty() && expr.template_type_args.empty()) {
            break;
        }
        const std::string lowered_template_args =
            !expr.template_type_args.empty()
                ? join_lowered_type_args(expr.template_type_args, aliases)
                : join_lowered_template_args(expr.template_args, aliases);
        const std::string lowered_call_args = join_lowered_exprs(expr.children, aliases, locals);
        if (expr.name == "new") {
            return "new " + lowered_template_args + "(" + lowered_call_args + ")";
        }
        if (expr.name == "malloc") {
            const std::string type = lowered_template_args;
            return "static_cast<" + type + "*>(std::malloc(sizeof(" + type + ") * (" +
                   lowered_call_args + ")))";
        }
        if (starts_with(expr.name, "*")) {
            const std::string pointee = !expr.template_type_args.empty()
                                            ? trim_copy(expr.name.substr(1)) + "[" +
                                                  join_type_arg_texts(expr.template_type_args) + "]"
                                            : trim_copy(expr.name.substr(1)) + "[" +
                                                  join_template_arg_texts(expr.template_args) + "]";
            return "reinterpret_cast<" + lower_cpp_type("*" + pointee, aliases) + ">(" +
                   lowered_call_args + ")";
        }
        if (expr.name == "sizeof" || expr.name == "alignof") {
            return expr.name + "(" + lowered_template_args + ")";
        }
        if (expr.name == "offsetof" && expr.children.size() == 1) {
            return "offsetof(" + lowered_template_args + ", " +
                   lower_offsetof_field(expr.children.front(), aliases, locals) + ")";
        }
        if ((expr.name == "list" || expr.name == "dict" || expr.name == "set") &&
            expr.children.empty()) {
            const std::string type_args = !expr.template_type_args.empty()
                                              ? join_type_arg_texts(expr.template_type_args)
                                              : join_template_arg_texts(expr.template_args);
            return lower_cpp_type(expr.name + "[" + type_args + "]", aliases) + "{}";
        }
        return lower_callee_expr(expr, aliases, locals) + "<" + lowered_template_args + ">(" +
               lowered_call_args + ")";
    }
    case ExprKind::Member:
        if (expr.children.size() == 1) {
            if (const auto variant = enum_variant_from_expr(symbols, expr)) {
                if (enum_has_payloads(*variant->first)) {
                    return lower_enum_variant_constructor(*variant->first, *variant->second, {},
                                                          aliases, locals, symbols);
                }
            }
            if (const auto swizzle = lower_swizzle_expr(expr, aliases, locals)) {
                return *swizzle;
            }
            if (is_pointer_receiver_expr(expr.children.front(), locals)) {
                return lower_expr(expr.children.front(), aliases, locals) + "->" + expr.name;
            }
            return lower_member_expr(lower_expr(expr.children.front(), aliases, locals), expr.name,
                                     aliases);
        }
        break;
    case ExprKind::DictEntry:
        if (expr.children.size() == 2) {
            return "{" + lower_expr(expr.children[0], aliases, locals) + ", " +
                   lower_expr(expr.children[1], aliases, locals) + "}";
        }
        break;
    case ExprKind::NamedArg:
        if (expr.children.size() == 1) {
            return "." + expr.name + " = " + lower_expr(expr.children.front(), aliases, locals);
        }
        break;
    case ExprKind::Slice:
        throw CompileError(expr.location, "slice expression must be used inside an index");
    case ExprKind::DictLiteral:
        return "{" + join_lowered_exprs(expr.children, aliases, locals) + "}";
    case ExprKind::Index:
        if (expr.children.size() == 2) {
            std::string out = lower_expr(expr.children[0], aliases, locals);
            if (expr.children[1].kind == ExprKind::Slice && expr.children[1].children.size() == 2) {
                const Expr& start_expr = expr.children[1].children[0];
                const Expr& end_expr = expr.children[1].children[1];
                const std::string start =
                    start_expr.text.empty() ? "0" : lower_expr(start_expr, aliases, locals);
                const std::string end = end_expr.text.empty()
                                            ? "(" + out + ").size()"
                                            : lower_expr(end_expr, aliases, locals);
                return "std::span(&(" + out + ")[" + start + "], (" + end + ") - (" + start + "))";
            }
            if (expr.children[1].kind == ExprKind::TupleLiteral) {
                if (const auto slice = lower_trailing_full_slice_expr(
                        expr.children[0], expr.children[1], aliases, locals, symbols)) {
                    return *slice;
                }
                for (const Expr& index : expr.children[1].children) {
                    out += "[" + lower_expr(index, aliases, locals) + "]";
                }
                return out;
            }
            return out + "[" + lower_expr(expr.children[1], aliases, locals) + "]";
        }
        break;
    case ExprKind::ListLiteral:
    case ExprKind::SetLiteral:
        return "{" + join_lowered_exprs(expr.children, aliases, locals) + "}";
    case ExprKind::TupleLiteral:
        return join_lowered_exprs(expr.children, aliases, locals);
    case ExprKind::Lambda:
        throw CompileError(expr.location,
                           "unsupported Python feature: lambda; declare a named function and "
                           "pass the function name");
    case ExprKind::Unknown:
        throw CompileError(expr.location, "unsupported expression: " + trim_copy(expr.text));
    }
    return {};
}

std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const std::map<std::string, std::string>& locals) {
    if (expr.kind != ExprKind::ListLiteral) {
        return lower_expr(expr, aliases, locals);
    }
    std::ostringstream out;
    out << "{";
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_array_literal(expr.children[i], aliases, locals);
    }
    out << "}";
    return out.str();
}

bool is_build_value_expr(const Expr& expr);

bool is_build_member_expr(const Expr& expr) {
    return expr.kind == ExprKind::Member && expr.children.size() == 1 &&
           expr.children.front().kind == ExprKind::Name && expr.children.front().name == "build";
}

bool is_build_only_condition(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::StringLiteral:
        return true;
    case ExprKind::Member:
        return is_build_member_expr(expr);
    case ExprKind::Unary:
        return expr.children.size() == 1 && expr.op == "not" &&
               is_build_only_condition(expr.children.front());
    case ExprKind::Binary:
        if (expr.children.size() != 2) {
            return false;
        }
        if (expr.op == "and" || expr.op == "or" || expr.op == "==" || expr.op == "!=" ||
            expr.op == "<" || expr.op == "<=" || expr.op == ">" || expr.op == ">=") {
            return is_build_value_expr(expr.children[0]) && is_build_value_expr(expr.children[1]);
        }
        return false;
    default:
        return false;
    }
}

bool is_build_value_expr(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::BoolLiteral:
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::StringLiteral:
        return true;
    case ExprKind::Member:
        return is_build_member_expr(expr);
    case ExprKind::Unary:
        return expr.children.size() == 1 && expr.op == "not" &&
               is_build_value_expr(expr.children.front());
    case ExprKind::Binary:
        return is_build_only_condition(expr);
    default:
        return false;
    }
}

std::string if_keyword_for_condition(const Expr& condition) {
    return is_build_only_condition(condition) ? "if constexpr" : "if";
}

bool is_wildcard_pattern_expr(const Expr& expr) {
    return expr.kind == ExprKind::Name && expr.name == "_";
}

std::optional<std::string> enum_case_variant_name(const Stmt& stmt) {
    if (is_wildcard_pattern_expr(stmt.pattern_expr)) {
        return std::string{"_"};
    }
    const Expr* pattern = &stmt.pattern_expr;
    if (stmt.pattern_expr.kind == ExprKind::Call && !stmt.pattern_expr.callee.empty()) {
        pattern = &stmt.pattern_expr.callee.front();
    }
    const std::optional<std::string> path = member_path_from_expr(*pattern);
    if (!path) {
        return std::nullopt;
    }
    const size_t dot = path->find('.');
    if (dot == std::string::npos || path->find('.', dot + 1) != std::string::npos) {
        return std::nullopt;
    }
    return path->substr(dot + 1);
}

struct EnumCaseBinding {
    size_t field_index = 0;
    std::string name;
};

std::vector<EnumCaseBinding> enum_case_bindings(const Stmt& stmt, const EnumValueDecl& value) {
    std::vector<EnumCaseBinding> out;
    if (stmt.pattern_expr.kind != ExprKind::Call) {
        return out;
    }
    for (size_t i = 0; i < stmt.pattern_expr.children.size(); ++i) {
        const Expr& child = stmt.pattern_expr.children[i];
        if (child.kind == ExprKind::Name && !child.name.empty()) {
            out.push_back({.field_index = i, .name = child.name});
        } else if (child.kind == ExprKind::NamedArg && child.children.size() == 1 &&
                   child.children.front().kind == ExprKind::Name &&
                   !child.children.front().name.empty()) {
            const auto found = std::find_if(
                value.payload_fields.begin(), value.payload_fields.end(),
                [&](const EnumPayloadField& field) { return field.name == child.name; });
            if (found != value.payload_fields.end()) {
                out.push_back({.field_index = static_cast<size_t>(
                                   std::distance(value.payload_fields.begin(), found)),
                               .name = child.children.front().name});
            }
        }
    }
    return out;
}

std::string cpp_string_literal(std::string text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

bool match_cases_return(const Stmt& stmt) {
    if (stmt.children.empty()) {
        return false;
    }
    for (const Stmt& child : stmt.children) {
        if (child.kind != StmtKind::Case || !block_guarantees_return(child.children)) {
            return false;
        }
    }
    return true;
}

std::string lower_declared_stmt_type(const Stmt& stmt, const std::string& effective_type,
                                     const std::vector<std::string>& aliases) {
    return effective_type == stmt.type ? lower_cpp_type(stmt.type_ref, aliases)
                                       : lower_cpp_type(effective_type, aliases);
}

void emit_cpp_escape(std::ostringstream& out, const std::string& text, int depth) {
    std::istringstream body(cpp_escape_body(text));
    std::string line;
    while (std::getline(body, line)) {
        if (!trim_copy(line).empty()) {
            out << indent(depth) << trim_copy(line) << '\n';
        }
    }
}

void emit_source_comment(std::ostringstream& out, const Stmt& stmt, int depth) {
    out << indent(depth) << "// dudu: " << format_location(stmt.location) << '\n';
}

void emit_simple_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                           const std::vector<std::string>& aliases,
                           std::map<std::string, std::string>& locals,
                           const std::string& return_type,
                           const std::map<std::string, std::string>& function_returns,
                           const Symbols* symbols) {
    const std::string text = trim_copy(stmt.text);
    if (stmt.kind == StmtKind::CppEscape) {
        emit_cpp_escape(out, text, depth);
        return;
    }
    if (stmt.kind == StmtKind::Pass) {
        out << indent(depth) << "(void)0;\n";
        return;
    }
    if (stmt.kind == StmtKind::Break) {
        out << indent(depth) << "break;\n";
        return;
    }
    if (stmt.kind == StmtKind::Continue) {
        out << indent(depth) << "continue;\n";
        return;
    }
    if (stmt.kind == StmtKind::Raise) {
        out << indent(depth) << "throw";
        if (has_expr(stmt.value_expr)) {
            out << ' ' << lower_expr(stmt.value_expr, aliases, locals, symbols);
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Delete) {
        out << indent(depth) << "delete " << lower_expr(stmt.value_expr, aliases, locals, symbols)
            << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assert) {
        out << indent(depth) << "if (!("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols)
            << ")) { throw std::runtime_error(";
        if (has_expr(stmt.message_expr))
            out << lower_expr(stmt.message_expr, aliases, locals, symbols);
        else
            out << cpp_string_literal("assert failed: " + stmt.condition_expr.text);
        out << "); }\n";
        return;
    }
    if (stmt.kind == StmtKind::DebugAssert) {
        out << indent(depth) << "assert(("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ")";
        if (has_expr(stmt.message_expr))
            out << " && (" << lower_expr(stmt.message_expr, aliases, locals, symbols) << ")";
        out << ");\n";
        return;
    }
    if (stmt.kind == StmtKind::Return) {
        out << indent(depth) << "return";
        if (has_expr(stmt.value_expr)) {
            if (starts_with(return_type, "Option[") &&
                stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " std::nullopt";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " {" << lower_expr(stmt.value_expr, aliases, locals, symbols) << '}';
            } else {
                out << ' ' << lower_expr(stmt.value_expr, aliases, locals, symbols);
            }
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        const std::string& name = stmt.name;
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(stmt.type, stmt.value_expr);
        const std::string type =
            inferred.status == ArrayShapeStatus::Inferred ? inferred.type : stmt.type;
        locals[name] = type;
        out << indent(depth) << lower_declared_stmt_type(stmt, type, aliases) << ' ' << name;
        if (has_expr(stmt.value_expr)) {
            if (starts_with(type, "Option[") && stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " = std::nullopt";
            } else if (starts_with(type, "array[") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {" << lower_array_literal(stmt.value_expr, aliases, locals) << "}";
            } else if (starts_with(type, "list[") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral &&
                       stmt.value_expr.children.empty()) {
                out << " = {}";
            } else if (starts_with(type, "list[") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols)
                    << "}";
            } else if (starts_with(type, "dict[") &&
                       stmt.value_expr.kind == ExprKind::DictLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols)
                    << "}";
            } else if (starts_with(type, "set[") && stmt.value_expr.kind == ExprKind::SetLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols)
                    << "}";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " = " << lower_declared_stmt_type(stmt, type, aliases) << "{"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols)
                    << '}';
            } else {
                out << " = " << lower_expr(stmt.value_expr, aliases, locals, symbols);
            }
        } else {
            out << "{}";
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (const std::vector<std::string> names = tuple_binding_names(stmt.target_expr);
            !names.empty()) {
            out << indent(depth) << "auto [" << join_names(names)
                << "] = " << lower_expr(stmt.value_expr, aliases, locals, symbols) << ";\n";
            return;
        }
        if (stmt.target_expr.kind == ExprKind::Name && !stmt.target_expr.name.empty()) {
            const std::string& lhs = stmt.target_expr.name;
            const std::string value = lower_expr(stmt.value_expr, aliases, locals, symbols);
            if (locals.contains(lhs)) {
                out << indent(depth) << lhs << " = ";
                if (starts_with(locals.at(lhs), "Option[") &&
                    stmt.value_expr.kind == ExprKind::NoneLiteral) {
                    out << "std::nullopt";
                } else {
                    out << value;
                }
                out << ";\n";
            } else {
                const std::string inferred =
                    infer_emitted_local_type(stmt.value_expr, locals, function_returns);
                locals.emplace(lhs, inferred.empty() ? "auto" : inferred);
                out << indent(depth) << "auto " << lhs << " = " << value << ";\n";
            }
            return;
        }
        if (stmt.target_expr.kind != ExprKind::Unknown) {
            if (const auto call = lower_index_assignment_hook(stmt, aliases, locals, symbols)) {
                out << indent(depth) << *call << ";\n";
                return;
            }
            out << indent(depth) << lower_expr(stmt.target_expr, aliases, locals, symbols) << " = "
                << lower_expr(stmt.value_expr, aliases, locals, symbols) << ";\n";
            return;
        }
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        out << indent(depth) << lower_expr(stmt.target_expr, aliases, locals, symbols) << ' '
            << stmt.op << '=' << " " << lower_expr(stmt.value_expr, aliases, locals, symbols)
            << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Expr) {
        out << indent(depth) << lower_expr(stmt.expr, aliases, locals, symbols) << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Unknown) {
        throw CompileError(stmt.location, "unsupported statement: " + trim_copy(stmt.text));
    }
}

void emit_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                    const std::vector<std::string>& aliases,
                    std::map<std::string, std::string>& locals, const std::string& return_type,
                    const std::map<std::string, std::string>& function_returns,
                    const Symbols* symbols) {
    emit_source_comment(out, stmt, depth);
    if (stmt.kind == StmtKind::If) {
        out << indent(depth) << if_keyword_for_condition(stmt.condition_expr) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        out << indent(depth) << "else " << if_keyword_for_condition(stmt.condition_expr) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Else) {
        out << indent(depth) << "else {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Match) {
        const std::string subject_type =
            infer_emitted_local_type(stmt.condition_expr, locals, function_returns);
        if (!subject_type.empty()) {
            const EnumDecl* en = enum_decl_for_type(symbols, subject_type);
            if (en != nullptr && enum_has_payloads(*en)) {
                const std::string subject = "__dudu_match_" + std::to_string(stmt.location.line) +
                                            "_" + std::to_string(stmt.location.column);
                out << indent(depth) << "auto&& " << subject << " = "
                    << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ";\n";
                bool first_case = true;
                for (const Stmt& child : stmt.children) {
                    if (child.kind != StmtKind::Case) {
                        continue;
                    }
                    const std::optional<std::string> variant = enum_case_variant_name(child);
                    if (!variant || *variant == "_") {
                        out << indent(depth) << (first_case ? "if" : "else") << " (true) {\n";
                    } else {
                        out << indent(depth) << (first_case ? "if" : "else if") << " ("
                            << "std::holds_alternative<" << en->name << "::" << *variant << ">("
                            << subject << ".value)) {\n";
                    }
                    std::map<std::string, std::string> nested = locals;
                    if (variant && *variant != "_") {
                        if (const EnumValueDecl* value = enum_variant_decl(*en, *variant)) {
                            const std::string payload = "__dudu_case_" +
                                                        std::to_string(child.location.line) + "_" +
                                                        std::to_string(child.location.column);
                            out << indent(depth + 1) << "auto&& " << payload << " = std::get<"
                                << en->name << "::" << *variant << ">(" << subject << ".value);\n";
                            const std::vector<EnumCaseBinding> bindings =
                                enum_case_bindings(child, *value);
                            for (const EnumCaseBinding& binding : bindings) {
                                if (binding.field_index >= value->payload_fields.size()) {
                                    continue;
                                }
                                const EnumPayloadField& field =
                                    value->payload_fields[binding.field_index];
                                nested[binding.name] = field.type;
                                out << indent(depth + 1) << "auto&& " << binding.name << " = "
                                    << payload << "." << field.name << ";\n";
                            }
                        }
                    }
                    emit_block(out, child.children, depth + 1, aliases, nested, return_type,
                               function_returns, symbols);
                    out << indent(depth) << "}\n";
                    first_case = false;
                }
                if (match_cases_return(stmt)) {
                    out << indent(depth) << "__builtin_unreachable();\n";
                }
                return;
            }
        }
        out << indent(depth) << "switch ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ") {\n";
        for (const Stmt& child : stmt.children) {
            if (child.kind != StmtKind::Case) {
                continue;
            }
            if (is_wildcard_pattern_expr(child.pattern_expr)) {
                out << indent(depth) << "default:\n";
            } else {
                out << indent(depth) << "case "
                    << lower_expr(child.pattern_expr, aliases, locals, symbols) << ":\n";
            }
            out << indent(depth + 1) << "{\n";
            emit_block(out, child.children, depth + 2, aliases, locals, return_type,
                       function_returns, symbols);
            out << indent(depth + 2) << "break;\n";
            out << indent(depth + 1) << "}\n";
        }
        out << indent(depth) << "}\n";
        if (match_cases_return(stmt)) {
            out << indent(depth) << "__builtin_unreachable();\n";
        }
        return;
    }
    if (stmt.kind == StmtKind::Try) {
        out << indent(depth) << "try {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        out << indent(depth);
        if (stmt.name.empty() || stmt.type.empty()) {
            out << "catch (...)";
        } else {
            out << "catch (const " << lower_cpp_type(stmt.type_ref, aliases) << "& " << stmt.name
                << ")";
        }
        out << " {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::While) {
        out << indent(depth) << "while ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::For && has_expr(stmt.iterable_expr)) {
        std::string binding = stmt.name;
        const std::string range = lower_expr(stmt.iterable_expr, aliases, locals, symbols);
        std::string binding_type = "auto";
        if (!stmt.type.empty()) {
            binding_type = lower_cpp_type(stmt.type_ref, aliases);
            locals[stmt.name] = stmt.type;
        }
        if (stmt.iterable_expr.kind == ExprKind::Call && stmt.iterable_expr.name == "range") {
            const std::vector<Expr>& args = stmt.iterable_expr.children;
            const std::string start =
                args.size() == 1 ? "0" : lower_expr(args.at(0), aliases, locals, symbols);
            const std::string end = args.size() == 1
                                        ? lower_expr(args.at(0), aliases, locals, symbols)
                                        : lower_expr(args.at(1), aliases, locals, symbols);
            const std::string step =
                args.size() >= 3 ? lower_expr(args.at(2), aliases, locals, symbols) : "1";
            out << indent(depth) << "for (" << binding_type << ' ' << binding << " = " << start
                << "; " << binding << " < " << end << "; " << binding << " += " << step << ") {\n";
            emit_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                       function_returns, symbols);
            out << indent(depth) << "}\n";
            return;
        }
        const std::string loop_type = stmt.type.empty() ? "auto&&" : binding_type;
        out << indent(depth) << "for (" << loop_type << ' ' << binding << " : " << range << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    emit_simple_statement(out, stmt, depth, aliases, locals, return_type, function_returns,
                          symbols);
}

} // namespace

std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals) {
    return lower_expr(expr, aliases, locals);
}

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases) {
    emit_block(out, body, depth, aliases, {});
}

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases,
                const std::map<std::string, std::string>& initial_locals,
                const std::string& return_type,
                const std::map<std::string, std::string>& function_returns,
                const Symbols* symbols) {
    std::map<std::string, std::string> locals = initial_locals;
    for (const Stmt& stmt : body) {
        emit_statement(out, stmt, depth, aliases, locals, return_type, function_returns, symbols);
    }
}

} // namespace dudu
