#include "dudu/escapes.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema.hpp"

#include <cctype>

namespace dudu {
namespace {

bool is_borrowed_or_pointer(std::string type) {
    type = trim_copy(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    return parsed.kind == TypeKind::Reference || parsed.kind == TypeKind::Pointer;
}

void fail_if_value_local_escapes(const SourceLocation& location,
                                 const std::map<std::string, std::string>& locals,
                                 const std::string& name) {
    const auto it = locals.find(name);
    if (it != locals.end() && !is_borrowed_or_pointer(it->second)) {
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
                                            const std::map<std::string, std::string>& locals,
                                            const Expr& expr) {
    std::string name;
    if (is_address_of_local(expr, name)) {
        fail_if_value_local_escapes(location, locals, name);
    }
}

void check_return_address_escape(const std::map<std::string, std::string>& locals,
                                 const Expr& expr) {
    std::string name;
    if (is_address_of_local(expr, name)) {
        fail_if_value_local_escapes(expr.location, locals, name);
        return;
    }
    for (const Expr& child : expr.children) {
        check_return_address_escape(locals, child);
    }
}

} // namespace

void check_local_address_escape(const Stmt& stmt,
                                const std::map<std::string, std::string>& locals) {
    if (stmt.kind == StmtKind::Return) {
        check_return_address_escape(locals, stmt.value_expr);
        return;
    }
    if (stmt.kind == StmtKind::Expr && stmt.expr.kind == ExprKind::Call) {
        const std::optional<std::string> member = member_callee_name(stmt.expr);
        if (member == "append" || member == "push_back") {
            for (const Expr& arg : stmt.expr.children) {
                fail_if_address_of_value_local_escapes(arg.location, locals, arg);
            }
        }
    }
}

} // namespace dudu
