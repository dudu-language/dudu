#include "dudu/lsp/language_server_operator.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr.hpp"
#include "dudu/sema/sema_scope.hpp"
#include "dudu/sema/type_compat.hpp"

#include <optional>
#include <set>
#include <string>

namespace dudu {
namespace {

std::optional<std::string> operator_name_for_method(const FunctionDecl& method) {
    for (const Decorator& decorator : method.decorators) {
        if (const std::optional<std::string> op =
                decorator_first_string_arg(decorator, "operator")) {
            return op;
        }
    }
    return std::nullopt;
}

bool operator_method_accepts_rhs(const FunctionDecl& method, const Expr& right_expr,
                                 const TypeRef& right_type) {
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    if (method.params.size() <= first_param) {
        return true;
    }
    return assignment_type_allowed(method.params[first_param].type_ref, right_expr, right_type);
}

} // namespace

bool dudu_operator_query_exists(const ModuleAst& module, const std::string& query) {
    if (query.find('.') == std::string::npos) {
        return false;
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (klass.name + "." + method.name != query) {
                continue;
            }
            if (operator_name_for_method(method).has_value()) {
                return true;
            }
        }
    }
    return false;
}

std::optional<Symbol> dudu_operator_symbol_for_expr(const ModuleAst& module, const Expr& expr,
                                                    int one_based_line) {
    if (expr.kind != ExprKind::Binary || expr.children.size() != 2) {
        return std::nullopt;
    }
    try {
        Symbols symbols = collect_symbols(module);
        FunctionScope scope(symbols);
        scope.local_type_refs = local_type_refs_before_line(module, one_based_line);
        const TypeRef left_type = infer_expr_type_ast(scope, expr.children[0], nullptr);
        const TypeRef right_type = infer_expr_type_ast(scope, expr.children[1], nullptr);
        if (!has_type_ref(left_type) || !has_type_ref(right_type)) {
            return std::nullopt;
        }
        const std::set<std::string> candidate_types = member_candidate_types(module, left_type);
        for (const ClassDecl& klass : module.classes) {
            if (!candidate_types.contains(klass.name)) {
                continue;
            }
            for (const FunctionDecl& method : klass.methods) {
                const std::optional<std::string> op = operator_name_for_method(method);
                if (!op || *op != std::string(expr.op)) {
                    continue;
                }
                if (!operator_method_accepts_rhs(method, expr.children[1], right_type)) {
                    continue;
                }
                return Symbol{.name = klass.name + "." + method.name,
                              .detail = function_detail(method),
                              .location = method.location,
                              .kind = lsp_symbol_kind::Method,
                              .native_identity_key = std::nullopt,
                              .doc_comment = method.doc_comment};
            }
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

} // namespace dudu
