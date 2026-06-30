#include "dudu/codegen/cpp_expr_call_emit.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema_enum.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/sema_scope.hpp"
#include "dudu/core/source.hpp"

#include <algorithm>
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
    static const std::vector<std::string_view> types = {"bool",  "char", "i8",  "i16", "i32",
                                                        "i64",   "u8",   "u16", "u32", "u64",
                                                        "usize", "f32",  "f64", "str", "cstr"};
    return std::find(types.begin(), types.end(), name) != types.end();
}

bool pointer_cast_type_ref_like(const TypeRef& type, const Symbols* symbols) {
    const std::string name = type_ref_head_name(type);
    for (std::string_view tag : {"struct ", "class ", "union ", "enum "}) {
        if (name.starts_with(tag)) {
            return true;
        }
    }
    if (type_ref_is_name(type, "struct")) {
        return true;
    }
    if (is_builtin_cast_call(name)) {
        return true;
    }
    switch (type.kind) {
    case TypeKind::Template:
    case TypeKind::Qualified:
    case TypeKind::FixedArray:
    case TypeKind::Shaped:
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
        return symbols != nullptr && known_type_ref(*symbols, type);
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::PackExpansion:
    case TypeKind::Value:
    case TypeKind::Unknown:
        return false;
    }
    return false;
}

std::optional<TypeRef> pointer_cast_pointee_type_ref(const Expr& expr, const Symbols* symbols) {
    if (!has_expr_type_ref(expr)) {
        return std::nullopt;
    }
    const TypeRef& type_ref = expr_type_ref(expr);
    if (!pointer_cast_type_ref_like(type_ref, symbols)) {
        return std::nullopt;
    }
    return type_ref;
}

TypeRef pointer_type_ref_from_pointee(TypeRef type, SourceLocation location) {
    return wrapped_type_ref(TypeKind::Pointer, std::move(type), location);
}

std::string lower_call_args_for_signature(const std::vector<Expr>& args, const FunctionSignature&,
                                          const std::vector<std::string>& aliases,
                                          const CppLocalContext& locals,
                                          const std::map<std::string, TypeRef>& local_type_refs,
                                          const Symbols* symbols, const CppEmitOptions& options) {
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_expr(args[i], aliases, locals, local_type_refs, symbols, options);
    }
    return out.str();
}

bool expression_has_pointer_type(const Expr& expr,
                                 const std::map<std::string, TypeRef>& local_type_refs,
                                 const Symbols* symbols) {
    if (is_pointer_receiver_expr(expr, local_type_refs)) {
        return true;
    }
    if (symbols == nullptr) {
        return false;
    }
    const TypeRef type = member_expr_type_ref(*symbols, local_type_refs, nullptr, expr);
    return type.kind == TypeKind::Pointer;
}

std::vector<Expr> index_arg_exprs(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        return index_expr.children;
    }
    return {index_expr};
}

std::vector<TypeRef> infer_index_arg_type_refs(
    const std::vector<Expr>& args, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols) {
    std::vector<TypeRef> out;
    out.reserve(args.size());
    const std::map<std::string, TypeRef> function_returns;
    for (const Expr& arg : args) {
        out.push_back(infer_emitted_local_type_ref(arg, local_type_refs, function_returns, symbols));
    }
    return out;
}

std::string lower_named_argument_call(const Expr& expr, const std::vector<std::string>& aliases,
                                      const CppLocalContext& locals,
                                      const std::map<std::string, TypeRef>& local_type_refs,
                                      const Symbols* symbols, const CppEmitOptions& options) {
    std::ostringstream out;
    out << lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options) << "{";
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (expr.children[i].kind == ExprKind::NamedArg && expr.children[i].children.size() == 1) {
            out << "." << expr.children[i].name << " = "
                << lower_expr(expr.children[i].children.front(), aliases, locals, local_type_refs,
                              symbols, options);
            continue;
        }
        out << lower_expr(expr.children[i], aliases, locals, local_type_refs, symbols, options);
    }
    out << "}";
    return out.str();
}

} // namespace

bool is_builtin_template_constructor(std::string_view name) {
    static const std::vector<std::string_view> types = {"list",   "dict", "set",
                                                        "atomic", "span", "variant"};
    return std::find(types.begin(), types.end(), name) != types.end();
}

std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const CppLocalContext& locals, const Symbols* symbols,
                              const CppEmitOptions& options) {
    return lower_callee_expr(expr, aliases, locals, {}, symbols, options);
}

std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const CppLocalContext& locals,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const Symbols* symbols, const CppEmitOptions& options) {
    if (has_expr_callee(expr)) {
        const Expr& callee = expr_callee(expr).front();
        if (callee.kind == ExprKind::Member && callee.children.size() == 1 &&
            callee.children.front().kind == ExprKind::Name &&
            callee.children.front().name == "super") {
            if (!locals.super_class.empty()) {
                return locals.super_class + "::" + callee.name;
            }
        }
        if (callee.kind == ExprKind::Member && callee.children.size() == 1 &&
            expression_has_pointer_type(callee.children.front(), local_type_refs, symbols)) {
            return lower_expr(callee.children.front(), aliases, locals, local_type_refs, symbols,
                              options) +
                   "->" + callee.name;
        }
    }
    if (has_expr_callee(expr)) {
        return lower_expr(expr_callee(expr).front(), aliases, locals, local_type_refs, symbols,
                          options);
    }
    return locals.contains(expr.name) ? expr.name.str() : emitted_value_name(expr.name, options);
}

std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const CppLocalContext& locals, const Symbols* symbols) {
    return lower_callee_expr(expr, aliases, locals, symbols, {});
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
                                           const CppLocalContext& locals, const Symbols* symbols,
                                           const CppEmitOptions& options) {
    return lower_enum_variant_constructor(en, value, args, aliases, locals, {}, symbols, options);
}

std::string lower_enum_variant_constructor(const EnumDecl& en, const EnumValueDecl& value,
                                           const std::vector<Expr>& args,
                                           const std::vector<std::string>& aliases,
                                           const CppLocalContext& locals,
                                           const std::map<std::string, TypeRef>& local_type_refs,
                                           const Symbols* symbols, const CppEmitOptions& options) {
    std::ostringstream out;
    const std::string enum_name = emitted_type_name(en.name, options);
    out << enum_name << "{" << enum_name << "::" << value.name << "{";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (args[i].kind == ExprKind::NamedArg && args[i].children.size() == 1) {
            out << "." << args[i].name << " = "
                << lower_expr(args[i].children.front(), aliases, locals, local_type_refs, symbols,
                              options);
        } else {
            out << lower_expr(args[i], aliases, locals, local_type_refs, symbols, options);
        }
    }
    out << "}}";
    return out.str();
}

std::string lower_enum_variant_constructor(const EnumDecl& en, const EnumValueDecl& value,
                                           const std::vector<Expr>& args,
                                           const std::vector<std::string>& aliases,
                                           const CppLocalContext& locals, const Symbols* symbols) {
    return lower_enum_variant_constructor(en, value, args, aliases, locals, symbols, {});
}

std::optional<std::string>
lower_index_assignment_hook(const Stmt& stmt, const std::vector<std::string>& aliases,
                            const CppLocalContext& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const Symbols* symbols, const CppEmitOptions& options) {
    if (symbols == nullptr || stmt_target_expr(stmt).kind != ExprKind::Index ||
        stmt_target_expr(stmt).children.size() != 2) {
        return std::nullopt;
    }
    const Expr& receiver = stmt_target_expr(stmt).children[0];
    TypeRef receiver_type;
    if (receiver.kind == ExprKind::Name) {
        receiver_type = local_type_ref(local_type_refs, receiver.name, receiver.location);
    } else {
        receiver_type = member_expr_type_ref(*symbols, local_type_refs, nullptr, receiver, {},
                                             locals.current_class);
    }
    if (!has_type_ref(receiver_type)) {
        return std::nullopt;
    }
    std::vector<Expr> args = index_arg_exprs(stmt_target_expr(stmt).children[1]);
    args.push_back(stmt.value_expr);
    const auto method = dudu_operator_method_name_for_args(
        *symbols, "[]=", receiver_type, args,
        infer_index_arg_type_refs(args, local_type_refs, symbols));
    if (!method) {
        return std::nullopt;
    }
    const std::string lowered_receiver =
        lower_expr(receiver, aliases, locals, local_type_refs, symbols, options);
    const std::string access = receiver_type.kind == TypeKind::Pointer ? "->" : ".";
    return lowered_receiver + access + *method + "(" +
           join_lowered_exprs(args, aliases, locals, local_type_refs, ", ", symbols, options) + ")";
}

std::optional<std::string>
lower_index_read_hook(const Expr& expr, const std::vector<std::string>& aliases,
                      const CppLocalContext& locals,
                      const std::map<std::string, TypeRef>& local_type_refs, const Symbols* symbols,
                      const CppEmitOptions& options) {
    if (symbols == nullptr || expr.kind != ExprKind::Index || expr.children.size() != 2) {
        return std::nullopt;
    }
    const Expr& receiver = expr.children[0];
    TypeRef receiver_type;
    if (receiver.kind == ExprKind::Name) {
        receiver_type = local_type_ref(local_type_refs, receiver.name, receiver.location);
    } else {
        receiver_type = member_expr_type_ref(*symbols, local_type_refs, nullptr, receiver, {},
                                             locals.current_class);
    }
    if (!has_type_ref(receiver_type)) {
        return std::nullopt;
    }
    const std::vector<Expr> args = index_arg_exprs(expr.children[1]);
    const auto method = dudu_operator_method_name_for_args(
        *symbols, "[]", receiver_type, args,
        infer_index_arg_type_refs(args, local_type_refs, symbols));
    if (!method) {
        return std::nullopt;
    }
    const std::string lowered_receiver =
        lower_expr(receiver, aliases, locals, local_type_refs, symbols, options);
    const std::string access = receiver_type.kind == TypeKind::Pointer ? "->" : ".";
    return lowered_receiver + access + *method + "(" +
           join_lowered_exprs(args, aliases, locals, local_type_refs, ", ", symbols, options) + ")";
}

std::optional<std::string> lower_pointer_cast_expr(const Expr& expr,
                                                   const std::vector<std::string>& aliases,
                                                   const CppLocalContext& locals,
                                                   const Symbols* symbols,
                                                   const CppEmitOptions& options) {
    return lower_pointer_cast_expr(expr, aliases, locals, {}, symbols, options);
}

std::optional<std::string>
lower_pointer_cast_expr(const Expr& expr, const std::vector<std::string>& aliases,
                        const CppLocalContext& locals,
                        const std::map<std::string, TypeRef>& local_type_refs,
                        const Symbols* symbols, const CppEmitOptions& options) {
    if (expr.op != "*" || expr.children.size() != 1 ||
        expr.children.front().kind != ExprKind::Call) {
        return std::nullopt;
    }
    const Expr& call = expr.children.front();
    const std::optional<TypeRef> pointee = pointer_cast_pointee_type_ref(call, symbols);
    if (!pointee) {
        return std::nullopt;
    }
    return "reinterpret_cast<" +
           lower_cpp_type(pointer_type_ref_from_pointee(*pointee, expr.location), aliases,
                          options) +
           ">(" +
           join_lowered_exprs(call.children, aliases, locals, local_type_refs, ", ", symbols,
                              options) +
           ")";
}

std::optional<std::string> lower_pointer_cast_expr(const Expr& expr,
                                                   const std::vector<std::string>& aliases,
                                                   const CppLocalContext& locals,
                                                   const Symbols* symbols) {
    return lower_pointer_cast_expr(expr, aliases, locals, symbols, {});
}

std::string lower_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                            const CppLocalContext& locals, const Symbols* symbols,
                            const CppEmitOptions& options) {
    return lower_call_expr(expr, aliases, locals, {}, symbols, options);
}

std::string lower_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                            const CppLocalContext& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const Symbols* symbols, const CppEmitOptions& options) {
    if (has_expr_callee(expr)) {
        if (symbols != nullptr) {
            if (const auto variant = enum_variant_from_expr(*symbols, expr_callee(expr).front())) {
                if (enum_has_payloads(*variant->first)) {
                    return lower_enum_variant_constructor(*variant->first, *variant->second,
                                                          expr.children, aliases, locals,
                                                          local_type_refs, symbols, options);
                }
            }
        }
    }
    if (has_named_argument_shape(expr.children)) {
        return lower_named_argument_call(expr, aliases, locals, local_type_refs, symbols, options);
    }
    const std::string callee_name = direct_callee_name(expr);
    if (starts_with(callee_name, "*")) {
        if (const std::optional<TypeRef> pointee = pointer_cast_pointee_type_ref(expr, symbols)) {
            return "reinterpret_cast<" +
                   lower_cpp_type(pointer_type_ref_from_pointee(*pointee, expr.location), aliases,
                                  options) +
                   ">(" +
                   join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ",
                                      symbols, options) +
                   ")";
        }
    }
    if (callee_name == "len" && expr.children.size() == 1) {
        return "(" +
               lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                          options) +
               ").size()";
    }
    if (callee_name == "str" && expr.children.size() == 1) {
        if (expr.children.front().kind == ExprKind::StringLiteral) {
            return "std::string(" +
                   lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                              options) +
                   ")";
        }
        return "std::to_string(" +
               lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                          options) +
               ")";
    }
    if ((callee_name == "Ok" || callee_name == "Err") && expr.children.size() == 1) {
        return "dudu::" + callee_name + "(" +
               join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                                  options) +
               ")";
    }
    if (callee_name == "cstr" && expr.children.size() == 1) {
        return "reinterpret_cast<const char*>(" +
               lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                          options) +
               ")";
    }
    if (is_builtin_cast_call(callee_name)) {
        return lower_cpp_type(named_type_ref(callee_name), aliases) + "(" +
               join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                                  options) +
               ")";
    }
    std::string callee =
        lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options);
    if (ends_with(callee, ".append")) {
        callee.replace(callee.size() - 7, 7, ".push_back");
    }
    if (symbols != nullptr) {
        if (const auto signature = symbols->function_signatures.find(callee_name);
            signature != symbols->function_signatures.end()) {
            return callee + "(" +
                   lower_call_args_for_signature(expr.children, signature->second, aliases, locals,
                                                 local_type_refs, symbols, options) +
                   ")";
        }
    }
    return callee + "(" +
           join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                              options) +
           ")";
}

std::string lower_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                            const CppLocalContext& locals, const Symbols* symbols) {
    return lower_call_expr(expr, aliases, locals, symbols, {});
}

} // namespace dudu
