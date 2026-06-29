#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_expr_call_emit.hpp"
#include "dudu/sema/sema_scope.hpp"

namespace dudu {
namespace {

bool is_pointer_type_ref(const TypeRef& type) {
    return type.kind == TypeKind::Pointer;
}

bool is_pointer_list_type_ref(const TypeRef& type) {
    return type.kind == TypeKind::Template && type.name == "list" && type.children.size() == 1 &&
           type.children.front().kind == TypeKind::Pointer;
}

} // namespace

bool is_pointer_receiver_expr(const Expr& expr,
                              const std::map<std::string, TypeRef>& local_type_refs) {
    if (expr.kind == ExprKind::Name) {
        if (const auto typed = local_type_refs.find(expr.name); typed != local_type_refs.end()) {
            return is_pointer_type_ref(typed->second);
        }
        return false;
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2 &&
        expr.children.front().kind == ExprKind::Name) {
        const std::string& name = expr.children.front().name;
        if (const auto typed = local_type_refs.find(name); typed != local_type_refs.end()) {
            return is_pointer_list_type_ref(typed->second);
        }
        return false;
    }
    return false;
}

} // namespace dudu
