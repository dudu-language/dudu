#include "dudu/cpp_expr_emit.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_expr_call_emit.hpp"
#include "dudu/cpp_expr_slices.hpp"
#include "dudu/cpp_expr_swizzles.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/sema_context.hpp"
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
std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals, const Symbols* symbols,
                       const CppEmitOptions& options);
std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals,
                       const std::map<std::string, TypeRef>& local_type_refs,
                       const Symbols* symbols, const CppEmitOptions& options);

std::string lower_name_expr(const std::string& name, const CppLocalContext& locals,
                            const CppEmitOptions& options) {
    if (!locals.contains(name)) {
        return emitted_value_name(name, options);
    }
    return name;
}

std::string lower_member_expr(std::string receiver, const std::string& member,
                              const std::vector<std::string>& aliases,
                              const CppEmitOptions& options) {
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
    const std::string generated = emitted_value_name(dotted, options);
    if (generated != dotted) {
        return generated;
    }
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
                               const CppLocalContext& locals, std::string_view separator,
                               const Symbols* symbols, const CppEmitOptions& options) {
    return join_lowered_exprs(exprs, aliases, locals, {}, separator, symbols, options);
}

std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const CppLocalContext& locals,
                               const std::map<std::string, TypeRef>& local_type_refs,
                               std::string_view separator, const Symbols* symbols,
                               const CppEmitOptions& options) {
    std::ostringstream out;
    for (size_t i = 0; i < exprs.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << lower_expr(exprs[i], aliases, locals, local_type_refs, symbols, options);
    }
    return out.str();
}

std::string join_lowered_exprs(const std::vector<Expr>& exprs,
                               const std::vector<std::string>& aliases,
                               const CppLocalContext& locals, std::string_view separator,
                               const Symbols* symbols) {
    return join_lowered_exprs(exprs, aliases, locals, separator, symbols, {});
}

bool has_expr(const Expr& expr) {
    return expr_present(expr);
}

struct SliceParts {
    const Expr* start = nullptr;
    const Expr* end = nullptr;
    const Expr* step = nullptr;
};

std::optional<SliceParts> slice_parts(const Expr& expr) {
    if (expr.kind != ExprKind::Slice || expr.children.size() != 2) {
        return std::nullopt;
    }
    SliceParts parts{.start = &expr.children[0], .end = &expr.children[1], .step = nullptr};
    if (expr.children[1].kind == ExprKind::Slice && expr.children[1].children.size() == 2) {
        parts.end = &expr.children[1].children[0];
        parts.step = &expr.children[1].children[1];
    }
    return parts;
}

std::string join_lowered_type_args(const std::vector<TypeRef>& types,
                                   const std::vector<std::string>& aliases,
                                   const CppEmitOptions& options) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(types[i], aliases, options);
    }
    return out.str();
}

TypeKind wrapper_template_kind(std::string_view name) {
    const TypeKind wrapper = wrapper_type_kind(name);
    return wrapper == TypeKind::Unknown ? TypeKind::Template : wrapper;
}

TypeRef template_type_ref_from_expr(const Expr& expr, std::string name) {
    TypeRef type;
    type.kind =
        expr.template_type_args.size() == 1 ? wrapper_template_kind(name) : TypeKind::Template;
    type.name = std::move(name);
    type.children = expr.template_type_args;
    type.location = expr.location;
    type.range = expr.range;
    return type;
}

TypeRef pointer_template_type_ref_from_expr(const Expr& expr) {
    const std::string callee = direct_callee_name(expr);
    TypeRef pointee =
        template_type_ref_from_expr(expr, callee.size() > 1 ? trim_copy(callee.substr(1)) : "");

    TypeRef pointer = wrapped_type_ref(TypeKind::Pointer, std::move(pointee), expr.location);
    pointer.range = expr.range;
    return pointer;
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

std::string parsed_literal_value(const Expr& expr, std::string_view name) {
    if (expr.value.empty()) {
        throw CompileError(expr.location, "malformed " + std::string(name) +
                                              " literal node: missing parsed value");
    }
    return expr.value;
}

std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals, const Symbols* symbols,
                       const CppEmitOptions& options) {
    return lower_expr(expr, aliases, locals, {}, symbols, options);
}

std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals,
                       const std::map<std::string, TypeRef>& local_type_refs,
                       const Symbols* symbols, const CppEmitOptions& options) {
    if (expr.kind == ExprKind::Missing) {
        return {};
    }
    if (expr.kind == ExprKind::Unknown) {
        throw CompileError(expr.location, "unsupported expression: " + trim_copy(expr.text));
    }
    switch (expr.kind) {
    case ExprKind::BoolLiteral: {
        const std::string value = parsed_literal_value(expr, "bool");
        return value == "True" ? "true" : "false";
    }
    case ExprKind::NoneLiteral:
        return "nullptr";
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
        return lower_numeric_separators(
            parsed_literal_value(expr, expr.kind == ExprKind::IntLiteral ? "integer" : "float"));
    case ExprKind::Name:
        if (expr.name == "class") {
            if (!locals.current_class.empty()) {
                return locals.current_class;
            }
        }
        return lower_name_expr(expr.name, locals, options);
    case ExprKind::CppEscape:
        return lower_cpp_escape_expr(expr.value, aliases, local_type_refs);
    case ExprKind::StringLiteral:
        return expr.text;
    case ExprKind::Unary:
        if (const auto pointer_cast =
                lower_pointer_cast_expr(expr, aliases, locals, local_type_refs, symbols, options)) {
            return *pointer_cast;
        }
        if (expr.children.size() == 1) {
            const std::string op = expr.op == "not" ? "!" : expr.op;
            return "(" + op +
                   lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                              options) +
                   ")";
        }
        break;
    case ExprKind::Binary:
        if (expr.children.size() == 2 && has_expr(expr.children[0]) && has_expr(expr.children[1])) {
            return "(" +
                   lower_expr(expr.children[0], aliases, locals, local_type_refs, symbols,
                              options) +
                   " " + cpp_binary_operator(expr.op) + " " +
                   lower_expr(expr.children[1], aliases, locals, local_type_refs, symbols,
                              options) +
                   ")";
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
        return lower_call_expr(expr, aliases, locals, local_type_refs, symbols, options);
    case ExprKind::TemplateCall: {
        if (expr.template_type_args.empty()) {
            throw CompileError(expr.location,
                               "malformed template call: missing parsed type arguments");
        }
        const std::string lowered_template_args =
            join_lowered_type_args(expr.template_type_args, aliases, options);
        const std::string lowered_call_args = join_lowered_exprs(
            expr.children, aliases, locals, local_type_refs, ", ", symbols, options);
        const std::string callee = direct_callee_name(expr);
        if (callee == "new") {
            return "new " + lowered_template_args + "(" + lowered_call_args + ")";
        }
        if (callee == "malloc") {
            const std::string type = lowered_template_args;
            return "static_cast<" + type + "*>(std::malloc(sizeof(" + type + ") * (" +
                   lowered_call_args + ")))";
        }
        if (starts_with(callee, "*")) {
            return "reinterpret_cast<" +
                   lower_cpp_type(pointer_template_type_ref_from_expr(expr), aliases, options) +
                   ">(" + lowered_call_args + ")";
        }
        if (callee == "sizeof" || callee == "alignof") {
            return callee + "(" + lowered_template_args + ")";
        }
        if (callee == "offsetof" && expr.children.size() == 1) {
            return "offsetof(" + lowered_template_args + ", " +
                   lower_offsetof_field(expr.children.front(), aliases, locals, local_type_refs,
                                        symbols, options) +
                   ")";
        }
        if ((callee == "std.function" || callee == "std::function") &&
            expr.template_type_args.size() == 1) {
            const std::string type =
                lower_cpp_type(template_type_ref_from_expr(expr, callee), aliases, options);
            return expr.children.empty() ? type + "{}" : type + "(" + lowered_call_args + ")";
        }
        if (is_builtin_template_constructor(callee)) {
            const std::string type =
                lower_cpp_type(template_type_ref_from_expr(expr, callee), aliases, options);
            return expr.children.empty() ? type + "{}" : type + "(" + lowered_call_args + ")";
        }
        return lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options) + "<" +
               lowered_template_args + ">(" + lowered_call_args + ")";
    }
    case ExprKind::Member:
        if (expr.children.size() == 1) {
            if (symbols != nullptr) {
                if (const auto variant = enum_variant_from_expr(*symbols, expr)) {
                    if (enum_has_payloads(*variant->first)) {
                        return lower_enum_variant_constructor(*variant->first, *variant->second, {},
                                                              aliases, locals, local_type_refs,
                                                              symbols, options);
                    }
                }
            }
            if (const auto swizzle =
                    lower_swizzle_expr(expr, aliases, locals, local_type_refs, symbols, options)) {
                return *swizzle;
            }
            if (is_pointer_receiver_expr(expr.children.front(), local_type_refs)) {
                return lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                                  options) +
                       "->" + expr.name;
            }
            if (symbols != nullptr) {
                const TypeRef receiver_type =
                    member_expr_type_ref(*symbols, local_type_refs, nullptr, expr.children.front());
                if (has_type_ref(receiver_type) &&
                    field_type_ref_for_type(*symbols, receiver_type, expr.name)) {
                    return lower_expr(expr.children.front(), aliases, locals, local_type_refs,
                                      symbols, options) +
                           "." + expr.name;
                }
            }
            return lower_member_expr(lower_expr(expr.children.front(), aliases, locals,
                                                local_type_refs, symbols, options),
                                     expr.name, aliases, options);
        }
        break;
    case ExprKind::DictEntry:
        if (expr.children.size() == 2) {
            return "{" +
                   lower_expr(expr.children[0], aliases, locals, local_type_refs, symbols,
                              options) +
                   ", " +
                   lower_expr(expr.children[1], aliases, locals, local_type_refs, symbols,
                              options) +
                   "}";
        }
        break;
    case ExprKind::NamedArg:
        if (expr.children.size() == 1) {
            return "." + expr.name + " = " +
                   lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                              options);
        }
        break;
    case ExprKind::Slice:
        throw CompileError(expr.location, "slice expression must be used inside an index");
    case ExprKind::DictLiteral:
        return "{" +
               join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                                  options) +
               "}";
    case ExprKind::Index:
        if (expr.children.size() == 2) {
            std::string out =
                lower_expr(expr.children[0], aliases, locals, local_type_refs, symbols, options);
            if (const std::optional<SliceParts> slice = slice_parts(expr.children[1])) {
                const std::string start = expr_missing(*slice->start)
                                              ? "0"
                                              : lower_expr(*slice->start, aliases, locals,
                                                           local_type_refs, symbols, options);
                const std::string end = expr_missing(*slice->end)
                                            ? "(" + out + ").size()"
                                            : lower_expr(*slice->end, aliases, locals,
                                                         local_type_refs, symbols, options);
                if (slice->step != nullptr) {
                    const std::string step = expr_missing(*slice->step)
                                                 ? "1"
                                                 : lower_expr(*slice->step, aliases, locals,
                                                              local_type_refs, symbols, options);
                    return "dudu::StridedSpan{&(" + out + ")[" + start + "], ((" + end + ") - (" +
                           start + ") + (" + step + ") - 1) / (" + step + "), " + step + "}";
                }
                return "std::span(&(" + out + ")[" + start + "], (" + end + ") - (" + start + "))";
            }
            if (expr.children[1].kind == ExprKind::TupleLiteral) {
                if (const auto channel_slice =
                        lower_channel_slice_expr(expr.children[0], expr.children[1], aliases,
                                                 locals, local_type_refs, symbols, options)) {
                    return *channel_slice;
                }
                if (const auto column_slice =
                        lower_column_slice_expr(expr.children[0], expr.children[1], aliases, locals,
                                                local_type_refs, symbols, options)) {
                    return *column_slice;
                }
                if (const auto slice =
                        lower_trailing_full_slice_expr(expr.children[0], expr.children[1], aliases,
                                                       locals, local_type_refs, symbols, options)) {
                    return *slice;
                }
                for (const Expr& index : expr.children[1].children) {
                    out += "[" +
                           lower_expr(index, aliases, locals, local_type_refs, symbols, options) +
                           "]";
                }
                return out;
            }
            return out + "[" +
                   lower_expr(expr.children[1], aliases, locals, local_type_refs, symbols,
                              options) +
                   "]";
        }
        break;
    case ExprKind::ListLiteral:
    case ExprKind::SetLiteral:
        return "{" +
               join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                                  options) +
               "}";
    case ExprKind::TupleLiteral:
        return join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                                  options);
    case ExprKind::DefExpression:
        throw CompileError(expr.location, "unsupported Python feature: def expressions");
    case ExprKind::Comprehension:
        throw CompileError(expr.location, "unsupported Python feature: comprehensions");
    case ExprKind::Lambda:
        throw CompileError(expr.location,
                           "unsupported Python feature: lambda; declare a named function and "
                           "pass the function name");
    case ExprKind::Missing:
        return {};
    case ExprKind::Unknown:
        throw CompileError(expr.location, "unsupported expression: " + trim_copy(expr.text));
    }
    return {};
}

std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const CppLocalContext& locals, const Symbols* symbols,
                                const CppEmitOptions& options) {
    return lower_array_literal(expr, aliases, locals, {}, symbols, options);
}

std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const CppLocalContext& locals,
                                const std::map<std::string, TypeRef>& local_type_refs,
                                const Symbols* symbols, const CppEmitOptions& options) {
    if (expr.kind != ExprKind::ListLiteral) {
        return lower_expr(expr, aliases, locals, local_type_refs, symbols, options);
    }
    std::ostringstream out;
    out << "{";
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_array_literal(expr.children[i], aliases, locals, local_type_refs, symbols,
                                   options);
    }
    out << "}";
    return out.str();
}

std::string lower_array_literal(const Expr& expr, const std::vector<std::string>& aliases,
                                const CppLocalContext& locals, const Symbols* symbols) {
    return lower_array_literal(expr, aliases, locals, symbols, {});
}

std::string lower_expr(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals, const Symbols* symbols) {
    return lower_expr(expr, aliases, locals, symbols, {});
}

std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const CppLocalContext& locals) {
    return lower_expr(expr, aliases, locals);
}

std::string lower_cpp_expr_ast(const Expr& expr, const std::vector<std::string>& aliases,
                               const CppLocalContext& locals, const CppEmitOptions& options) {
    return lower_expr(expr, aliases, locals, nullptr, options);
}

} // namespace dudu
