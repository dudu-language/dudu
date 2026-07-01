#include "dudu/codegen/cpp_expr_emit.hpp"

#include "dudu/codegen/cpp_expr_call_emit.hpp"
#include "dudu/codegen/cpp_expr_index.hpp"
#include "dudu/codegen/cpp_expr_slices.hpp"
#include "dudu/codegen/cpp_expr_swizzles.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/parser/ast_parse_utils.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_enum.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
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

std::vector<TypeRef> filter_template_type_args(const std::vector<TypeRef>& type_args,
                                               const std::vector<std::string>& generic_params,
                                               const std::vector<std::string>& cpp_params) {
    if (cpp_params.size() >= type_args.size()) {
        return type_args;
    }
    std::vector<TypeRef> filtered;
    filtered.reserve(cpp_params.size());
    for (const std::string& param : cpp_params) {
        const auto found = std::find(generic_params.begin(), generic_params.end(), param);
        if (found != generic_params.end()) {
            const size_t index = static_cast<size_t>(found - generic_params.begin());
            if (index < type_args.size()) {
                filtered.push_back(type_args[index]);
            }
        }
    }
    return filtered;
}

std::vector<TypeRef>
filter_member_template_type_args(const Expr& expr, std::vector<TypeRef> type_args,
                                 const std::map<std::string, TypeRef>& local_type_refs,
                                 const Symbols& symbols) {
    if (!has_expr_callee(expr) || expr_callee(expr).front().kind != ExprKind::Member ||
        expr_callee(expr).front().children.size() != 1) {
        return type_args;
    }
    const Expr& member = expr_callee(expr).front();
    const TypeRef receiver_type =
        infer_emitted_local_type_ref(member.children.front(), local_type_refs, {}, &symbols);
    const ClassDecl* klass =
        has_type_ref(receiver_type) ? class_for_receiver_type(symbols, receiver_type) : nullptr;
    if (klass == nullptr) {
        return type_args;
    }
    for (const FunctionDecl& method : klass->methods) {
        if (method.name == member.name && !method.generic_params.empty()) {
            return filter_template_type_args(type_args, method.generic_params,
                                             generic_cpp_params_for_function(method));
        }
    }
    return type_args;
}

} // namespace

std::string emitted_member_name_for_expr(const Expr& member,
                                         const std::map<std::string, TypeRef>& local_type_refs,
                                         const Symbols* symbols, const CppEmitOptions& options) {
    if (symbols == nullptr || member.kind != ExprKind::Member || member.children.size() != 1) {
        return member.name;
    }
    if (!cpp_reserved_identifier(member.name)) {
        return member.name;
    }

    const Expr& receiver = member.children.front();
    const ClassDecl* klass = nullptr;
    if (receiver.kind == ExprKind::Name) {
        if (const auto found = symbols->classes.find(receiver.name);
            found != symbols->classes.end()) {
            klass = found->second;
        }
        if (klass == nullptr) {
            if (const auto alias = symbols->alias_type_refs.find(receiver.name);
                alias != symbols->alias_type_refs.end()) {
                klass = class_for_receiver_type(*symbols, alias->second);
            }
        }
    }
    if (klass == nullptr) {
        const TypeRef receiver_type =
            member_expr_type_ref(*symbols, local_type_refs, nullptr, receiver);
        klass = has_type_ref(receiver_type) ? class_for_receiver_type(*symbols, receiver_type)
                                            : nullptr;
    }
    if (klass == nullptr) {
        return member.name;
    }
    return emitted_reserved_member_name(klass->name, member.name, options);
}

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

std::string lower_string_literal_value(std::string_view value) {
    std::string out = "\"";
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '\\' && i + 1 < value.size()) {
            out.push_back(c);
            out.push_back(value[++i]);
            continue;
        }
        if (c == '"') {
            out += "\\\"";
            continue;
        }
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        if (c == '\r') {
            out += "\\r";
            continue;
        }
        out.push_back(c);
    }
    out += "\"";
    return out;
}

std::string lower_member_expr(std::string receiver, const std::string& member,
                              const std::vector<std::string>& aliases,
                              const CppEmitOptions& options, const bool scoped_receiver) {
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
    if (scoped_receiver) {
        return receiver + "::" + member;
    }
    return dotted;
}

bool member_receiver_is_scoped(const Expr& receiver, const Symbols* symbols,
                               const CppLocalContext& locals) {
    if (receiver.kind != ExprKind::Name) {
        const std::optional<ExprPath> path = expr_path_from_expr(receiver);
        if (!path || symbols == nullptr) {
            return false;
        }
        const std::string name = render_expr_path(*path);
        if (symbols->classes.contains(name) || symbols->enums.contains(name) ||
            symbols->native_classes.contains(name)) {
            return true;
        }
        const size_t dot = name.find('.');
        if (dot == std::string::npos) {
            return false;
        }
        const std::string prefix = name.substr(0, dot);
        return symbols->native_path_prefixes.contains(prefix) &&
               !symbols->module_import_prefixes.contains(prefix);
    }
    if (receiver.name == "class") {
        return !locals.current_class.empty();
    }
    if (symbols == nullptr) {
        return false;
    }
    return symbols->classes.contains(receiver.name) || symbols->enums.contains(receiver.name) ||
           symbols->native_classes.contains(receiver.name) ||
           symbols->native_path_prefixes.contains(receiver.name) ||
           symbols->module_import_prefixes.contains(receiver.name);
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
    type.kind = expr_template_type_args(expr).size() == 1 ? wrapper_template_kind(name)
                                                          : TypeKind::Template;
    type.name = std::move(name);
    type.children = expr_template_type_args(expr);
    type.location = expr.location;
    type.range = expr.range;
    return type;
}

TypeRef pointer_template_type_ref_from_expr(const Expr& expr) {
    if (!has_expr_type_ref(expr)) {
        throw CompileError(expr.location,
                           "malformed pointer cast expression: missing parsed target type");
    }

    TypeRef pointer = wrapped_type_ref(TypeKind::Pointer, expr_type_ref(expr), expr.location);
    pointer.range = expr.range;
    return pointer;
}

std::string cpp_binary_operator(std::string_view op) {
    if (op == "and") {
        return "&&";
    }
    if (op == "or") {
        return "||";
    }
    return std::string(op);
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
        throw CompileError(expr.location, "unsupported expression: " + display_expr(expr));
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
        return lower_string_literal_value(parsed_literal_value(expr, "string"));
    case ExprKind::Unary:
        if (const auto pointer_cast =
                lower_pointer_cast_expr(expr, aliases, locals, local_type_refs, symbols, options)) {
            return *pointer_cast;
        }
        if (expr.children.size() == 1) {
            const std::string op = expr.op == "not" ? "!" : std::string(expr.op);
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
        if (!has_expr_template_type_args(expr)) {
            throw CompileError(expr.location,
                               "malformed template call: missing parsed type arguments");
        }
        std::vector<TypeRef> template_type_args = expr_template_type_args(expr);
        const std::string callee = direct_callee_name(expr);
        if (symbols != nullptr) {
            if (const auto decl = symbols->function_decls.find(callee);
                decl != symbols->function_decls.end() && !decl->second->generic_params.empty()) {
                template_type_args =
                    filter_template_type_args(template_type_args, decl->second->generic_params,
                                              generic_cpp_params_for_function(*decl->second));
            }
            template_type_args = filter_member_template_type_args(
                expr, std::move(template_type_args), local_type_refs, *symbols);
        }
        const std::string lowered_template_args =
            join_lowered_type_args(template_type_args, aliases, options);
        const std::string lowered_call_args = join_lowered_exprs(
            expr.children, aliases, locals, local_type_refs, ", ", symbols, options);
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
            expr_template_type_args(expr).size() == 1) {
            const std::string type =
                lower_cpp_type(template_type_ref_from_expr(expr, callee), aliases, options);
            return expr.children.empty() ? type + "{}" : type + "(" + lowered_call_args + ")";
        }
        if (callee == "assume_shape" && expr.children.size() == 1) {
            return lowered_call_args;
        }
        if (is_builtin_template_constructor(callee)) {
            const std::string type =
                lower_cpp_type(template_type_ref_from_expr(expr, callee), aliases, options);
            return expr.children.empty() ? type + "{}" : type + "(" + lowered_call_args + ")";
        }
        if (lowered_template_args.empty()) {
            return lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options) +
                   "(" + lowered_call_args + ")";
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
                    const std::string access = receiver_type.kind == TypeKind::Pointer ? "->" : ".";
                    return lower_expr(expr.children.front(), aliases, locals, local_type_refs,
                                      symbols, options) +
                           access + expr.name;
                }
            }
            return lower_member_expr(
                lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                           options),
                emitted_member_name_for_expr(expr, local_type_refs, symbols, options), aliases,
                options, member_receiver_is_scoped(expr.children.front(), symbols, locals));
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
        return lower_slice_value_expr(expr, aliases, locals, local_type_refs, symbols, options);
    case ExprKind::Ellipsis:
        return "dudu::Ellipsis{}";
    case ExprKind::NewAxis:
        return "dudu::NewAxis{}";
    case ExprKind::PackExpansion:
        if (expr.children.size() == 1) {
            return lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols,
                              options) +
                   "...";
        }
        break;
    case ExprKind::DictLiteral:
        return "{" +
               join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                                  options) +
               "}";
    case ExprKind::Index:
        return lower_index_expr(expr, aliases, locals, local_type_refs, symbols, options);
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
        throw CompileError(expr.location, "unsupported expression: " + display_expr(expr));
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
