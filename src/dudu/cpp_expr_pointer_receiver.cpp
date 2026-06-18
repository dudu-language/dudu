#include "dudu/ast_type.hpp"
#include "dudu/cpp_expr_call_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_scope.hpp"

#include <utility>

namespace dudu {
namespace {

bool is_pointer_type_ref(const TypeRef& type) {
    return type.kind == TypeKind::Pointer;
}

bool is_pointer_type(std::string type) {
    type = trim_copy(std::move(type));
    return is_pointer_type_ref(parse_type_text(type));
}

bool is_pointer_list_type_ref(const TypeRef& type) {
    return type.kind == TypeKind::Template && type.name == "list" && type.children.size() == 1 &&
           type.children.front().kind == TypeKind::Pointer;
}

bool is_pointer_list_type(std::string type) {
    type = trim_copy(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    return is_pointer_list_type_ref(parsed);
}

} // namespace

bool is_pointer_receiver_expr(const Expr& expr, const std::map<std::string, std::string>& locals,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const Symbols* symbols) {
    if (expr.kind == ExprKind::Name) {
        const auto local = locals.find(expr.name);
        if (local == locals.end()) {
            return false;
        }
        if (symbols != nullptr) {
            const TypeRef type =
                local_type_ref(*symbols, locals, local_type_refs, expr.name, expr.location);
            if (has_type_ref(type)) {
                return is_pointer_type_ref(type);
            }
            return is_pointer_type(resolve_alias(*symbols, local->second));
        }
        if (const auto typed = local_type_refs.find(expr.name); typed != local_type_refs.end()) {
            return is_pointer_type_ref(typed->second);
        }
        return is_pointer_type(local->second);
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2 &&
        expr.children.front().kind == ExprKind::Name) {
        const std::string& name = expr.children.front().name;
        const auto local = locals.find(name);
        if (local == locals.end()) {
            return false;
        }
        if (symbols != nullptr) {
            const TypeRef type = local_type_ref(*symbols, locals, local_type_refs, name,
                                                expr.children.front().location);
            if (has_type_ref(type)) {
                return is_pointer_list_type_ref(type);
            }
            return is_pointer_list_type(resolve_alias(*symbols, local->second));
        }
        if (const auto typed = local_type_refs.find(name); typed != local_type_refs.end()) {
            return is_pointer_list_type_ref(typed->second);
        }
        return is_pointer_list_type(local->second);
    }
    return false;
}

} // namespace dudu
