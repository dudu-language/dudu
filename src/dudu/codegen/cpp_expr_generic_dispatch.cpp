#include "dudu/codegen/cpp_expr_generic_dispatch.hpp"

#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <algorithm>
#include <sstream>

namespace dudu {
namespace {

std::string lowered_arguments(const Expr& expr, const std::vector<std::string>& aliases,
                              const CppLocalContext& locals,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const Symbols* symbols, const CppEmitOptions& options) {
    return join_lowered_exprs(expr.children, aliases, locals, local_type_refs, ", ", symbols,
                              options);
}

std::string explicit_template_arguments(const Expr& expr, const std::vector<std::string>& aliases,
                                        const CppEmitOptions& options) {
    if (!has_expr_template_type_args(expr)) {
        return {};
    }
    std::ostringstream out;
    out << '<';
    const std::vector<TypeRef>& arguments = expr_template_type_args(expr);
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(arguments[i], aliases, options);
    }
    out << '>';
    return out.str();
}

std::string call_arguments(std::string prefix, const std::string& arguments) {
    if (!arguments.empty()) {
        prefix += ", " + arguments;
    }
    return prefix;
}

bool generic_receiver_type(const TypeRef& type, const Symbols& symbols,
                           const CppLocalContext& locals) {
    if (!has_type_ref(type)) {
        return false;
    }
    const TypeRef receiver = unwrap_receiver_type_ref(symbols, type);
    if (locals.contains_type(type_ref_head_name(receiver))) {
        return true;
    }
    if (type_ref_head_name(receiver) == "Self") {
        const auto current = symbols.classes.find(locals.current_class);
        if (current != symbols.classes.end() && !current->second->generic_params.empty()) {
            return true;
        }
    }
    if (type_ref_head_name(receiver) == locals.current_class) {
        const auto current = symbols.classes.find(locals.current_class);
        if (current != symbols.classes.end() && !current->second->generic_params.empty()) {
            return true;
        }
    }
    return std::ranges::any_of(receiver.children, [&](const TypeRef& child) {
        return generic_receiver_type(child, symbols, locals);
    });
}

std::string cpp_instance_method_name(std::string_view name) {
    return name == "append" ? "push_back" : std::string(name);
}

std::optional<std::string>
lower_static_dispatch(const Expr& expr, const Expr& member, const std::vector<std::string>& aliases,
                      const CppLocalContext& locals,
                      const std::map<std::string, TypeRef>& local_type_refs, const Symbols& symbols,
                      const CppEmitOptions& options) {
    const Expr& receiver = member.children.front();
    if (receiver.kind != ExprKind::Name || local_type_refs.contains(receiver.name) ||
        !locals.contains_type(receiver.name)) {
        return std::nullopt;
    }
    const std::string arguments =
        lowered_arguments(expr, aliases, locals, local_type_refs, &symbols, options);
    const std::string templates = explicit_template_arguments(expr, aliases, options);
    const std::string dispatch = "dudu_dispatch_static_" + member.name;
    const std::string tag = "std::type_identity<DuduReceiver>{}";
    const std::string dispatch_call =
        dispatch + templates + "(" + call_arguments(tag, arguments) + ")";
    const std::string member_call =
        "DuduReceiver::" +
        (templates.empty() ? member.name : "template " + member.name + templates) + "(" +
        arguments + ")";
    return "([&]<typename DuduReceiver>(std::type_identity<DuduReceiver>) -> decltype(auto) { "
           "if constexpr (requires { " +
           dispatch_call + "; }) return " + dispatch_call + "; else return " + member_call +
           "; }(std::type_identity<" + receiver.name + ">{}))";
}

std::optional<std::string>
lower_instance_dispatch(const Expr& expr, const Expr& member,
                        const std::vector<std::string>& aliases, const CppLocalContext& locals,
                        const std::map<std::string, TypeRef>& local_type_refs,
                        const Symbols& symbols, const CppEmitOptions& options) {
    const Expr& receiver = member.children.front();
    const TypeRef receiver_type =
        infer_emitted_local_type_ref(receiver, local_type_refs, {}, &symbols);
    if (!generic_receiver_type(receiver_type, symbols, locals)) {
        return std::nullopt;
    }
    const bool pointer = receiver_type.kind == TypeKind::Pointer;
    const std::string arguments =
        lowered_arguments(expr, aliases, locals, local_type_refs, &symbols, options);
    const std::string templates = explicit_template_arguments(expr, aliases, options);
    const std::string forwarded = "std::forward<DuduReceiver>(dudu_receiver)";
    const std::string dispatch_receiver = pointer ? "*" + forwarded : forwarded;
    const std::string dispatch_call = "dudu_dispatch_instance_" + member.name + templates + "(" +
                                      call_arguments(dispatch_receiver, arguments) + ")";
    const std::string access = pointer ? "->" : ".";
    const std::string cpp_method = cpp_instance_method_name(member.name);
    const std::string method =
        templates.empty() ? cpp_method : "template " + cpp_method + templates;
    const std::string member_call = forwarded + access + method + "(" + arguments + ")";
    const std::string lowered_receiver =
        lower_expr(receiver, aliases, locals, local_type_refs, &symbols, options);
    return "([&]<typename DuduReceiver>(DuduReceiver&& dudu_receiver) -> decltype(auto) { "
           "if constexpr (requires { " +
           dispatch_call + "; }) return " + dispatch_call + "; else return " + member_call +
           "; }(" + lowered_receiver + "))";
}

} // namespace

std::optional<std::string>
lower_generic_method_dispatch(const Expr& expr, const std::vector<std::string>& aliases,
                              const CppLocalContext& locals,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const Symbols* symbols, const CppEmitOptions& options) {
    if (symbols == nullptr || !has_expr_callee(expr) ||
        expr_callee(expr).front().kind != ExprKind::Member ||
        expr_callee(expr).front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr_callee(expr).front();
    if (const auto lowered = lower_static_dispatch(expr, member, aliases, locals, local_type_refs,
                                                   *symbols, options)) {
        return lowered;
    }
    return lower_instance_dispatch(expr, member, aliases, locals, local_type_refs, *symbols,
                                   options);
}

} // namespace dudu
