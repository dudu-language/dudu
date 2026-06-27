#include "dudu/sema_expr_cpp_escape_infer.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_methods_internal.hpp"

#include <utility>

namespace dudu::cpp_escape_infer_detail {
namespace {

bool is_function_type_ref(const TypeRef& type) {
    if (type.kind == TypeKind::Function) {
        return true;
    }
    return type.kind == TypeKind::Template && type.children.size() == 1 &&
           type.children.front().kind == TypeKind::Function;
}

TypeRef pointer_type_ref(TypeRef pointee, SourceLocation location) {
    return wrapped_type_ref(TypeKind::Pointer, std::move(pointee), location);
}

} // namespace

bool known_cpp_escape_type_ref(const Symbols& symbols, const TypeRef& type) {
    if (is_function_type_ref(type)) {
        return true;
    }
    return known_type_ref(symbols, type);
}

TypeRef cpp_escape_member_expr_type_ref(const FunctionScope& scope, const SourceLocation* location,
                                        const Expr& expr) {
    if (expr.kind == ExprKind::Unknown) {
        return {};
    }
    return member_expr_type_ref(scope.symbols, scope.local_type_refs, location, expr);
}

TypeRef cpp_escape_member_path_type_ref(const FunctionScope& scope, const SourceLocation* location,
                                        const std::string& path) {
    const SourceLocation parse_location = location == nullptr ? SourceLocation{} : *location;
    const Expr expr = parse_expr_text(path, parse_location);
    return cpp_escape_member_expr_type_ref(scope, location, expr);
}

std::optional<TypeRef> infer_parsed_index_escape_ref(const FunctionScope& scope, const Expr& parsed,
                                                     const SourceLocation* location) {
    if (location == nullptr || parsed.kind != ExprKind::Index || parsed.children.size() != 2) {
        return std::nullopt;
    }
    const Expr& receiver = parsed.children[0];
    const Expr& index_expr = parsed.children[1];
    if (receiver.kind == ExprKind::Name && !receiver.name.empty()) {
        const TypeRef name_type = local_type_ref(scope, receiver.name, *location);
        if (has_type_ref(name_type)) {
            if (const auto signature = dudu_operator_signature(scope.symbols, "[]", name_type)) {
                check_call_args_ast(scope, receiver.name + "[]", *signature,
                                    index_arg_exprs(index_expr), location);
            }
        }
        return indexed_value_type_ref(scope.symbols, scope.local_type_refs, *location,
                                      receiver.name, index_expr,
                                      "indexed access to unknown local: ");
    }
    if (const auto path = expr_path_from_expr(receiver)) {
        const std::string name = render_expr_path(*path);
        const TypeRef receiver_type = cpp_escape_member_expr_type_ref(scope, location, receiver);
        if (has_type_ref(receiver_type)) {
            return indexed_type_ref_from_type(scope.symbols, *location, receiver_type, index_expr,
                                              name);
        }
    }
    return std::nullopt;
}

std::optional<TypeRef> infer_parsed_pointer_cast_escape_ref(const FunctionScope& scope,
                                                            const Expr& parsed,
                                                            const SourceLocation* location) {
    if (parsed.kind != ExprKind::Call || parsed.callee.size() != 1 ||
        parsed.callee.front().kind != ExprKind::Name ||
        !starts_with(parsed.callee.front().name, "*")) {
        return std::nullopt;
    }
    TypeRef type_ref = parsed.type_ref;
    if (!has_type_ref(type_ref)) {
        if (location != nullptr) {
            sema_expr_fail(parsed.callee.front().location, "malformed pointer cast type");
        }
        return std::nullopt;
    }
    if (const auto unknown = unknown_type_ref(scope.symbols, type_ref)) {
        if (location != nullptr) {
            const SourceLocation error_location =
                unknown->second.line > 0 ? unknown->second : parsed.callee.front().location;
            sema_expr_fail(error_location, "unknown pointer cast type: " + unknown->first);
        }
        return std::nullopt;
    }
    for (const Expr& arg : parsed.children) {
        (void)infer_expr_type_ast(scope, arg, location);
    }
    return pointer_type_ref(std::move(type_ref), parsed.callee.front().location);
}

std::optional<TypeRef> infer_parsed_unary_escape_ref(const FunctionScope& scope, const Expr& parsed,
                                                     const SourceLocation* location) {
    if (parsed.kind != ExprKind::Unary || parsed.children.size() != 1) {
        return std::nullopt;
    }
    const Expr& child = parsed.children.front();
    if (parsed.op == "*" && child.kind == ExprKind::Name) {
        const TypeRef type =
            local_type_ref(scope, child.name, location == nullptr ? child.location : *location);
        if (type.kind == TypeKind::Pointer && !type.children.empty()) {
            return type.children.front();
        }
        return std::nullopt;
    }
    if (parsed.op == "&") {
        const TypeRef type = infer_expr_type_ast(scope, child, location);
        if (has_type_ref(type)) {
            return pointer_type_ref(type, location == nullptr ? child.location : *location);
        }
    }
    return std::nullopt;
}

std::optional<TypeRef> infer_parsed_template_escape_ref(const FunctionScope& scope,
                                                        const Expr& parsed,
                                                        const SourceLocation* location) {
    if (parsed.kind != ExprKind::TemplateCall) {
        return std::nullopt;
    }
    const std::string callee = direct_callee_name(parsed);
    if (const auto allocation = infer_allocation_call_type_ref(
            scope.symbols, location, callee, template_type_refs(parsed), parsed.children.size())) {
        for (const Expr& arg : parsed.children) {
            (void)infer_expr_type_ast(scope, arg, location);
        }
        return *allocation;
    }
    return std::nullopt;
}

std::optional<TypeRef> infer_parsed_name_escape_ref(const FunctionScope& scope, const Expr& parsed,
                                                    const SourceLocation* location) {
    if (parsed.kind != ExprKind::Name) {
        return std::nullopt;
    }
    const SourceLocation name_location = location == nullptr ? parsed.location : *location;
    if (parsed.name == "None") {
        return named_type_ref("None", parsed.location);
    }
    if (const TypeRef local = local_type_ref(scope, parsed.name, name_location);
        has_type_ref(local)) {
        return local;
    }
    if (const auto fn = scope.symbols.function_signatures.find(parsed.name);
        fn != scope.symbols.function_signatures.end()) {
        return function_type_ref(fn->second, parsed.location);
    }
    if (const auto value = scope.symbols.native_value_type_refs.find(parsed.name);
        value != scope.symbols.native_value_type_refs.end()) {
        return value->second;
    }
    if (const auto native = scope.symbols.native_function_signatures.find(parsed.name);
        native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
        return function_type_ref(native->second.front(), parsed.location);
    }
    if (is_dudu_all_caps(parsed.name)) {
        return named_type_ref("i32", parsed.location);
    }
    return std::nullopt;
}

} // namespace dudu::cpp_escape_infer_detail
