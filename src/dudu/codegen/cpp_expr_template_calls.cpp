#include "dudu/codegen/cpp_expr_template_calls.hpp"

#include "dudu/codegen/cpp_expr_call_emit.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/sema/sema_constructors.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <utility>

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

bool uses_aggregate_new(const std::vector<TypeRef>& type_args, const Symbols* symbols) {
    if (symbols == nullptr || type_args.size() != 1) {
        return false;
    }
    const TypeRef allocated_type = resolve_alias_ref(*symbols, type_args.front());
    const auto found = symbols->classes.find(type_ref_head_name(allocated_type));
    return found != symbols->classes.end() && !found->second->native_declaration &&
           class_uses_aggregate_initialization(*found->second);
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

} // namespace

std::string lower_template_call_expr(const Expr& expr, const std::vector<std::string>& aliases,
                                     const CppLocalContext& locals,
                                     const std::map<std::string, TypeRef>& local_type_refs,
                                     const Symbols* symbols, const CppEmitOptions& options) {
    if (!has_expr_template_type_args(expr)) {
        throw CompileError(expr.location, "malformed template call: missing parsed type arguments");
    }
    if (const auto lowered =
            lower_enum_method_call(expr, aliases, locals, local_type_refs, symbols, options)) {
        return *lowered;
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
        template_type_args = filter_member_template_type_args(expr, std::move(template_type_args),
                                                              local_type_refs, *symbols);
    }

    const std::string lowered_template_args =
        join_lowered_type_args(template_type_args, aliases, options);
    const std::string lowered_call_args =
        join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols, options);
    if (callee == "new") {
        const bool aggregate = uses_aggregate_new(template_type_args, symbols);
        return "new " + lowered_template_args + (aggregate ? "{" : "(") + lowered_call_args +
               (aggregate ? "}" : ")");
    }
    if (callee == "malloc") {
        return "static_cast<" + lowered_template_args + "*>(std::malloc(sizeof(" +
               lowered_template_args + ") * (" + lowered_call_args + ")))";
    }
    if (starts_with(callee, "*")) {
        return "reinterpret_cast<" +
               lower_cpp_type(pointer_template_type_ref_from_expr(expr), aliases, options) + ">(" +
               lowered_call_args + ")";
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
    if (symbols != nullptr) {
        const TypeRef constructed_type = template_type_ref_from_expr(expr, callee);
        if (const ClassDecl* klass = class_for_receiver_type(*symbols, constructed_type);
            klass != nullptr && class_uses_aggregate_initialization(*klass)) {
            return lower_cpp_type(constructed_type, aliases, options) + "{" + lowered_call_args +
                   "}";
        }
    }
    if (lowered_template_args.empty()) {
        return lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options) + "(" +
               lowered_call_args + ")";
    }
    return lower_callee_expr(expr, aliases, locals, local_type_refs, symbols, options) + "<" +
           lowered_template_args + ">(" + lowered_call_args + ")";
}

} // namespace dudu
