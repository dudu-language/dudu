#include "dudu/core/escapes.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"

#include <cctype>
#include <utility>

namespace dudu {
namespace {

bool is_borrowed_or_pointer(TypeRef type) {
    while ((type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
            type.kind == TypeKind::Atomic || type.kind == TypeKind::Storage ||
            type.kind == TypeKind::Shared || type.kind == TypeKind::Device) &&
           type.children.size() == 1) {
        TypeRef child = type.children.front();
        type = std::move(child);
    }
    return type.kind == TypeKind::Reference || type.kind == TypeKind::Pointer;
}

void fail_if_value_local_escapes(const SourceLocation& location,
                                 const std::map<std::string, TypeRef>& local_type_refs,
                                 const std::string& name) {
    const auto it = local_type_refs.find(name);
    if (it != local_type_refs.end() && !is_borrowed_or_pointer(it->second)) {
        throw CompileError(location, "cannot let local address escape: " + name);
    }
}

bool is_address_of_local(const Expr& expr, std::string& name) {
    if (expr.kind != ExprKind::Unary || expr.op != "&" || expr.children.size() != 1) {
        return false;
    }
    const Expr& child = expr.children.front();
    if (child.kind != ExprKind::Name) {
        return false;
    }
    name = child.name;
    return true;
}

void fail_if_address_of_value_local_escapes(const SourceLocation& location,
                                            const std::map<std::string, TypeRef>& local_type_refs,
                                            const Expr& expr) {
    std::string name;
    if (is_address_of_local(expr, name)) {
        fail_if_value_local_escapes(location, local_type_refs, name);
    }
}

void check_return_address_escape(const std::map<std::string, TypeRef>& local_type_refs,
                                 const Expr& expr) {
    std::string name;
    if (is_address_of_local(expr, name)) {
        fail_if_value_local_escapes(expr.location, local_type_refs, name);
        return;
    }
    if (expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) {
        return;
    }
    for (const Expr& child : expr.children) {
        check_return_address_escape(local_type_refs, child);
    }
}

} // namespace

void check_local_address_escape(const Stmt& stmt,
                                const std::map<std::string, TypeRef>& local_type_refs) {
    if (stmt.kind == StmtKind::Return) {
        check_return_address_escape(local_type_refs, stmt.value_expr);
        return;
    }
    if (stmt.kind == StmtKind::Expr && stmt.expr.kind == ExprKind::Call) {
        const std::optional<std::string> member = member_callee_name(stmt.expr);
        if (member == "append" || member == "push_back") {
            for (const Expr& arg : stmt.expr.children) {
                fail_if_address_of_value_local_escapes(arg.location, local_type_refs, arg);
            }
        }
    }
}

} // namespace dudu
