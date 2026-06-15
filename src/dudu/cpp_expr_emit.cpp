#include "dudu/cpp_expr_emit.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/cpp_expr_call_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_pointer_members.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
std::string lower_cpp_escape_expr(std::string expr, const std::vector<std::string>& aliases,
                                  const std::map<std::string, std::string>& locals) {
    return lower_raw_cpp_escape_expr(rewrite_pointer_members(std::move(expr), locals), aliases);
}

std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const std::map<std::string, std::string>& locals, const Symbols* symbols);

std::string lower_name_expr(const std::string& name) {
    return name;
}

std::string lower_member_expr(std::string receiver, const std::string& member,
                              const std::vector<std::string>& aliases) {
    receiver = trim_copy(std::move(receiver));
    if (receiver.empty()) {
        return member;
    }
    if (receiver == "build") {
        return "build::" + member;
    }
    if (receiver == "shader") {
        return "shader::" + member;
    }
    if (starts_with(receiver, "shader::")) {
        return receiver + "." + member;
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
                               std::string_view separator, const Symbols* symbols) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << lower_expr(exprs[i], aliases, locals, symbols);
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
        if (is_builtin_template_constructor(expr.name)) {
            const std::string type_args = !expr.template_type_args.empty()
                                              ? join_type_arg_texts(expr.template_type_args)
                                              : join_template_arg_texts(expr.template_args);
            const std::string type = lower_cpp_type(expr.name + "[" + type_args + "]", aliases);
            return expr.children.empty() ? type + "{}" : type + "(" + lowered_call_args + ")";
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

std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals) {
    return lower_expr(expr, aliases, locals);
}

} // namespace dudu
