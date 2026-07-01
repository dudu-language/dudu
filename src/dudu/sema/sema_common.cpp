#include "dudu/sema/sema_common.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/source.hpp"
#include "dudu/sema/sema_generics.hpp"

namespace dudu {

[[noreturn]] void sema_fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message, "dudu.sema.error");
}

bool missing_expr(const Expr& expr) {
    return expr_missing(expr);
}

bool sema_has_expr(const Expr& expr) {
    return expr_present(expr);
}

const SourceLocation& diagnostic_location(const SourceLocation& context, const Expr& expr) {
    return expr.range.end.column > expr.range.start.column ? expr.location : context;
}

const SourceLocation& diagnostic_location(const SourceLocation& context, const TypeRef& type) {
    return type.range.end.column > type.range.start.column ? type.location : context;
}

void bind_local(FunctionScope& scope, const std::string& name, const TypeRef& type_ref) {
    if (has_type_ref(type_ref)) {
        scope.local_type_refs[name] = type_ref;
    } else {
        scope.local_type_refs.erase(name);
    }
}

Symbols with_generic_params(Symbols symbols, const std::vector<std::string>& params) {
    return with_generic_params(std::move(symbols), params, {});
}

Symbols with_generic_params(Symbols symbols, const std::vector<std::string>& params,
                            const std::set<std::string>& value_params) {
    for (const std::string& param : params) {
        const std::string name = generic_param_base_name(param);
        if (!value_params.contains(name)) {
            symbols.types.insert(name);
        }
        symbols.generic_params.insert(name);
    }
    return symbols;
}

Symbols with_self_type(Symbols symbols, const std::string& class_name) {
    if (class_name.empty()) {
        return symbols;
    }
    symbols.types.insert("Self");
    symbols.alias_type_refs["Self"] = named_type_ref(class_name);
    return symbols;
}

std::vector<Expr> index_arg_exprs(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        return index_expr.children;
    }
    return {index_expr};
}

std::optional<ExprPath> scoped_expr_path_from_expr(const FunctionScope& scope, const Expr& expr,
                                                   const SourceLocation* location) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        if (expr.name == "class") {
            if (!scope.current_class.empty()) {
                return ExprPath{.segments = {{.kind = ExprPathSegmentKind::Name,
                                              .text = scope.current_class,
                                              .location = expr.location}}};
            }
            if (location != nullptr) {
                sema_fail(*location, "class static access outside class");
            }
            return std::nullopt;
        }
        return ExprPath{
            .segments = {
                {.kind = ExprPathSegmentKind::Name, .text = expr.name, .location = expr.location}}};
    }
    if (expr.kind == ExprKind::Member && expr.children.size() == 1 && !expr.name.empty()) {
        std::optional<ExprPath> receiver =
            scoped_expr_path_from_expr(scope, expr.children.front(), location);
        if (receiver.has_value()) {
            receiver->segments.push_back(
                {.kind = ExprPathSegmentKind::Name, .text = expr.name, .location = expr.location});
            return receiver;
        }
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2) {
        std::optional<ExprPath> receiver =
            scoped_expr_path_from_expr(scope, expr.children.front(), location);
        const std::optional<std::string> index = path_index_from_expr(expr.children[1]);
        if (receiver.has_value() && index.has_value()) {
            receiver->segments.push_back({.kind = ExprPathSegmentKind::Index,
                                          .text = *index,
                                          .location = expr.children[1].location});
            return receiver;
        }
    }
    return std::nullopt;
}

std::optional<ExprPath> scoped_call_callee_path(const FunctionScope& scope, const Expr& expr,
                                                const SourceLocation* location) {
    if (has_expr_callee(expr)) {
        return scoped_expr_path_from_expr(scope, expr_callee(expr).front(), location);
    }
    return std::nullopt;
}

ScopedCallee scoped_call_callee(const FunctionScope& scope, const Expr& expr,
                                const SourceLocation* location) {
    ScopedCallee out;
    out.path = scoped_call_callee_path(scope, expr, location);
    out.key = out.path ? render_expr_path(*out.path) : "";
    return out;
}

} // namespace dudu
