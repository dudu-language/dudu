#include "dudu/codegen/cpp_expr_call_emit.hpp"

#include "dudu/codegen/cpp_emit_enum_methods.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_expr_generic_dispatch.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_emit_support.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/core/source.hpp"
#include "dudu/sema/sema_constructors.hpp"
#include "dudu/sema/sema_enum.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

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

bool has_declared_call_target(std::string_view name, const CppLocalContext& locals,
                              const Symbols* symbols) {
    if (locals.contains(name)) {
        return true;
    }
    if (symbols == nullptr) {
        return false;
    }
    const std::string key(name);
    return symbols->function_signatures.contains(key) ||
           symbols->function_overload_signatures.contains(key);
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
    case TypeKind::Associated:
    case TypeKind::AssociatedTemplate:
    case TypeKind::NativeTransform:
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

bool accepts_dudu_string(const TypeRef& type) {
    if (type_ref_is_name(type, "str")) {
        return true;
    }
    if ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const) &&
        type.children.size() == 1) {
        return accepts_dudu_string(type.children.front());
    }
    return false;
}

std::string lower_call_args_for_signature(const std::vector<Expr>& args,
                                          const FunctionSignature& signature,
                                          const std::vector<std::string>& aliases,
                                          const CppLocalContext& locals,
                                          const std::map<std::string, TypeRef>& local_type_refs,
                                          const Symbols* symbols, const CppEmitOptions& options) {
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        const TypeRef expected = signature_param_type_ref(
            signature, signature_param_index_for_arg(signature, i, args.size()));
        const std::string lowered =
            lower_expr(args[i], aliases, locals, local_type_refs, symbols, options);
        if (args[i].kind == ExprKind::StringLiteral && accepts_dudu_string(expected)) {
            out << "std::string(" << lowered << ')';
        } else {
            out << lowered;
        }
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

struct EnumMethodCall {
    const EnumDecl* en = nullptr;
    const FunctionDecl* method = nullptr;
    const Expr* receiver = nullptr;
    bool is_static = false;
    TypeRef receiver_type;
};

std::optional<EnumMethodCall>
enum_method_call(const Expr& expr, const std::map<std::string, TypeRef>& local_type_refs,
                 const Symbols* symbols) {
    if (symbols == nullptr || !has_expr_callee(expr) ||
        expr_callee(expr).front().kind != ExprKind::Member ||
        expr_callee(expr).front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr_callee(expr).front();
    const Expr& receiver = member.children.front();
    const EnumDecl* en = nullptr;
    TypeRef receiver_type;
    bool is_static = false;
    if (receiver.kind == ExprKind::Name && !local_type_refs.contains(receiver.name)) {
        const auto found = symbols->enums.find(receiver.name);
        if (found != symbols->enums.end()) {
            en = found->second;
            is_static = true;
            receiver_type = named_type_ref(en->name, receiver.location);
        }
    }
    if (en == nullptr) {
        receiver_type = infer_emitted_local_type_ref(receiver, local_type_refs, {}, symbols);
        en = enum_decl_for_type(*symbols, receiver_type);
    }
    if (en == nullptr)
        return std::nullopt;
    for (const FunctionDecl& method : en->methods) {
        const bool method_static = method.params.empty() || method.params.front().name != "self";
        if (method.name == member.name && method_static == is_static) {
            return EnumMethodCall{.en = en,
                                  .method = &method,
                                  .receiver = &receiver,
                                  .is_static = is_static,
                                  .receiver_type = receiver_type};
        }
    }
    return std::nullopt;
}

std::string lower_resolved_enum_method_call(const Expr& expr, const EnumMethodCall& target,
                                            const std::vector<std::string>& aliases,
                                            const CppLocalContext& locals,
                                            const std::map<std::string, TypeRef>& local_type_refs,
                                            const Symbols* symbols, const CppEmitOptions& options) {
    std::ostringstream out;
    out << emitted_enum_method_name(*target.en, *target.method, options);
    const std::vector<TypeRef>& type_args = expr_template_type_args(expr);
    if (!type_args.empty()) {
        out << '<';
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (i > 0)
                out << ", ";
            out << lower_cpp_type(type_args[i], aliases, options);
        }
        out << '>';
    }
    out << '(';
    bool emitted_arg = false;
    if (!target.is_static) {
        if (target.receiver_type.kind == TypeKind::Pointer)
            out << '*';
        out << lower_expr(*target.receiver, aliases, locals, local_type_refs, symbols, options);
        emitted_arg = true;
    }
    for (const Expr& arg : expr.children) {
        if (emitted_arg)
            out << ", ";
        out << lower_expr(arg, aliases, locals, local_type_refs, symbols, options);
        emitted_arg = true;
    }
    out << ')';
    return out.str();
}

std::string lower_named_argument_call(const Expr& expr, const std::vector<std::string>& aliases,
                                      const CppLocalContext& locals,
                                      const std::map<std::string, TypeRef>& local_type_refs,
                                      const Symbols* symbols, const CppEmitOptions& options) {
    std::ostringstream out;
    out << lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options) << "{";
    const std::optional<ExprPath> callee_path = call_callee_path(expr);
    const std::string callee = callee_path.has_value() ? render_expr_path(*callee_path) : "";
    const ClassDecl* target_class = nullptr;
    if (symbols != nullptr) {
        if (const auto found = symbols->classes.find(callee); found != symbols->classes.end())
            target_class = found->second;
    }
    if (target_class != nullptr && class_uses_aggregate_initialization(*target_class)) {
        std::map<std::string, const Expr*> values;
        size_t positional = 0;
        for (const Expr& arg : expr.children) {
            if (arg.kind == ExprKind::NamedArg && arg.children.size() == 1) {
                values[arg.name] = &arg.children.front();
            } else if (positional < target_class->fields.size()) {
                values[target_class->fields[positional].name] = &arg;
                ++positional;
            }
        }
        bool emitted = false;
        const std::map<std::string, TypeRef> function_returns;
        for (const FieldDecl& field : target_class->fields) {
            const auto value = values.find(field.name);
            if (value == values.end()) {
                continue;
            }
            if (emitted) {
                out << ", ";
            }
            out << "." << emitted_member_name(target_class->name, field.name, options) << " = ";
            if (is_fixed_array_type(field.type_ref) &&
                value->second->kind == ExprKind::ListLiteral) {
                out << lower_fixed_array_literal_as_type_ref(field.type_ref, *value->second,
                                                             aliases, locals, local_type_refs,
                                                             function_returns, symbols, options);
            } else {
                out << lower_expr_as_type_ref(field.type_ref, *value->second, aliases, locals,
                                              local_type_refs, function_returns, symbols, options);
            }
            emitted = true;
        }
        out << "}";
        return out.str();
    }
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (expr.children[i].kind == ExprKind::NamedArg && expr.children[i].children.size() == 1) {
            const std::string name =
                target_class == nullptr
                    ? expr.children[i].name
                    : emitted_member_name(target_class->name, expr.children[i].name, options);
            out << "." << name << " = "
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

std::optional<std::string>
lower_enum_method_call(const Expr& expr, const std::vector<std::string>& aliases,
                       const CppLocalContext& locals,
                       const std::map<std::string, TypeRef>& local_type_refs,
                       const Symbols* symbols, const CppEmitOptions& options) {
    const auto target = enum_method_call(expr, local_type_refs, symbols);
    if (!target) {
        return std::nullopt;
    }
    return lower_resolved_enum_method_call(expr, *target, aliases, locals, local_type_refs, symbols,
                                           options);
}

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
                   "->" + emitted_member_name_for_expr(callee, local_type_refs, symbols, options);
        }
    }
    if (has_expr_callee(expr)) {
        return lower_expr(expr_callee(expr).front(), aliases, locals, local_type_refs, symbols,
                          options);
    }
    return locals.contains(expr.name) ? locals.emitted(expr.name)
                                      : emitted_value_name(expr.name, options);
}

std::string lower_callee_expr(const Expr& expr, const std::vector<std::string>& aliases,
                              const CppLocalContext& locals, const Symbols* symbols) {
    return lower_callee_expr(expr, aliases, locals, symbols, {});
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
            out << "." << emitted_member_name(en.name + "_" + value.name, args[i].name, options)
                << " = "
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
           lower_cpp_type(wrapped_type_ref(TypeKind::Pointer, *pointee, expr.location), aliases,
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
    if (const auto lowered = lower_generic_method_dispatch(expr, aliases, locals, local_type_refs,
                                                           symbols, options)) {
        return *lowered;
    }
    if (const auto lowered =
            lower_enum_method_call(expr, aliases, locals, local_type_refs, symbols, options)) {
        return *lowered;
    }
    const std::string callee_name = direct_callee_name(expr);
    if (starts_with(callee_name, "*")) {
        if (const std::optional<TypeRef> pointee = pointer_cast_pointee_type_ref(expr, symbols)) {
            return "reinterpret_cast<" +
                   lower_cpp_type(wrapped_type_ref(TypeKind::Pointer, *pointee, expr.location),
                                  aliases, options) +
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
    if (callee_name == "move" && expr.children.size() == 1 &&
        !has_declared_call_target(callee_name, locals, symbols)) {
        return "std::move(" +
               lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                          options) +
               ")";
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
    if (symbols != nullptr) {
        const TypeRef constructed_type = named_type_ref(callee_name, expr.location);
        if (const ClassDecl* klass = class_for_receiver_type(*symbols, constructed_type);
            klass != nullptr && class_uses_aggregate_initialization(*klass)) {
            return lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options) +
                   "{" +
                   join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ",
                                      symbols, options) +
                   "}";
        }
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
