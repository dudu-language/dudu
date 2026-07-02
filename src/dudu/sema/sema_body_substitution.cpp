#include "dudu/sema/sema_body_substitution.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/shape_value_expr.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <map>
#include <sstream>

namespace dudu {
namespace {

TypeRef substitute_type_ref_pack_aware(const TypeRef& type,
                                       const BodyTypeSubstitutions& substitutions);

std::map<std::string, std::string>
scalar_binding_texts(const std::map<std::string, TypeRef>& substitutions) {
    std::map<std::string, std::string> out;
    for (const auto& [name, type] : substitutions) {
        out.emplace(name, substitute_type_ref_text(type, {}));
    }
    return out;
}

bool pack_expansion_name(const TypeRef& type, std::string& out) {
    if (type.kind != TypeKind::PackExpansion || type.children.size() != 1) {
        return false;
    }
    out = type_ref_head_name(type.children.front());
    return !out.empty();
}

std::vector<TypeRef>
substitute_type_ref_list_pack_aware(const std::vector<TypeRef>& types,
                                    const BodyTypeSubstitutions& substitutions) {
    std::vector<TypeRef> out;
    for (const TypeRef& type : types) {
        std::string pack_name;
        if (pack_expansion_name(type, pack_name)) {
            if (const auto pack = substitutions.packs.find(pack_name);
                pack != substitutions.packs.end()) {
                out.insert(out.end(), pack->second.begin(), pack->second.end());
                continue;
            }
        }
        out.push_back(substitute_type_ref_pack_aware(type, substitutions));
    }
    return out;
}

TypeRef substitute_type_ref_pack_aware(const TypeRef& type,
                                       const BodyTypeSubstitutions& substitutions) {
    const std::string key = type_ref_head_name(type);
    if (!key.empty()) {
        if (const auto found = substitutions.scalar.find(key);
            found != substitutions.scalar.end()) {
            if (type.kind == TypeKind::Template && !type.children.empty()) {
                TypeRef out = type;
                out.name = type_ref_head_name(found->second);
                out.location = type.location;
                out.children = substitute_type_ref_list_pack_aware(type.children, substitutions);
                return out;
            }
            TypeRef out = found->second;
            out.location = type.location;
            return out;
        }
    }
    if (type.kind == TypeKind::Value) {
        TypeRef out = type;
        out.value =
            shape_value_expr_substitute(type.value, scalar_binding_texts(substitutions.scalar));
        return out;
    }
    TypeRef out = type;
    out.children = substitute_type_ref_list_pack_aware(out.children, substitutions);
    return out;
}

BodyTypeSubstitutions
scalar_body_type_substitutions(const std::map<std::string, TypeRef>& substitutions) {
    BodyTypeSubstitutions out;
    out.scalar = substitutions;
    return out;
}

void substitute_expr_types(Expr& expr, const BodyTypeSubstitutions& substitutions) {
    if (const auto replacement = substitutions.scalar.find(expr.name);
        expr.kind == ExprKind::Name && replacement != substitutions.scalar.end()) {
        expr.name = substitute_type_ref_text(replacement->second, {});
    }
    if (has_expr_type_ref(expr)) {
        set_expr_type_ref(expr, substitute_type_ref_pack_aware(expr_type_ref(expr), substitutions));
    }
    if (has_expr_template_type_args(expr)) {
        std::vector<TypeRef> type_args = expr_template_type_args(expr);
        set_expr_template_type_args(expr,
                                    substitute_type_ref_list_pack_aware(type_args, substitutions));
    }
    for (Expr& child : expr.children) {
        substitute_expr_types(child, substitutions);
    }
    for (Expr& callee : mutable_expr_callee(expr)) {
        substitute_expr_types(callee, substitutions);
    }
    for (Expr& arg : mutable_expr_template_args(expr)) {
        substitute_expr_types(arg, substitutions);
    }
}

void substitute_stmt_types(Stmt& stmt, const BodyTypeSubstitutions& substitutions) {
    if (has_stmt_type_ref(stmt)) {
        set_stmt_type_ref(stmt, substitute_type_ref_pack_aware(stmt_type_ref(stmt), substitutions));
    }
    substitute_expr_types(stmt.expr, substitutions);
    substitute_expr_types(stmt.value_expr, substitutions);
    if (has_stmt_target_expr(stmt)) {
        Expr target = stmt_target_expr(stmt);
        substitute_expr_types(target, substitutions);
        set_stmt_target_expr(stmt, std::move(target));
    }
    if (has_stmt_condition_expr(stmt)) {
        Expr condition = stmt_condition_expr(stmt);
        substitute_expr_types(condition, substitutions);
        set_stmt_condition_expr(stmt, std::move(condition));
    }
    if (has_stmt_message_expr(stmt)) {
        Expr message = stmt_message_expr(stmt);
        substitute_expr_types(message, substitutions);
        set_stmt_message_expr(stmt, std::move(message));
    }
    if (has_stmt_iterable_expr(stmt)) {
        Expr iterable = stmt_iterable_expr(stmt);
        substitute_expr_types(iterable, substitutions);
        set_stmt_iterable_expr(stmt, std::move(iterable));
    }
    if (has_stmt_pattern_expr(stmt)) {
        Expr pattern = stmt_pattern_expr(stmt);
        substitute_expr_types(pattern, substitutions);
        set_stmt_pattern_expr(stmt, std::move(pattern));
    }
    if (has_stmt_guard_expr(stmt)) {
        Expr guard = stmt_guard_expr(stmt);
        substitute_expr_types(guard, substitutions);
        set_stmt_guard_expr(stmt, std::move(guard));
    }
    for (Stmt& child : stmt.children) {
        substitute_stmt_types(child, substitutions);
    }
}

} // namespace

BodyTypeSubstitutions body_type_substitutions(const std::vector<std::string>& params,
                                              const std::vector<TypeRef>& args) {
    BodyTypeSubstitutions out;
    size_t arg_index = 0;
    for (size_t i = 0; i < params.size() && arg_index < args.size(); ++i) {
        const std::string name = generic_param_base_name(params[i]);
        if (generic_param_is_pack(params[i])) {
            out.scalar.emplace(name, named_type_ref("auto"));
            out.packs.emplace(name, std::vector<TypeRef>(args.begin() + arg_index, args.end()));
            break;
        }
        out.scalar.emplace(name, args[arg_index]);
        ++arg_index;
    }
    return out;
}

std::string body_instantiated_label(const std::string& name, const std::vector<TypeRef>& args) {
    std::ostringstream out;
    out << name << "[";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << substitute_type_ref_text(args[i], {});
    }
    out << "]";
    return out.str();
}

std::vector<Stmt> substitute_body_types(std::vector<Stmt> body,
                                        const std::map<std::string, TypeRef>& substitutions) {
    return substitute_body_types(std::move(body), scalar_body_type_substitutions(substitutions));
}

std::vector<Stmt> substitute_body_types(std::vector<Stmt> body,
                                        const BodyTypeSubstitutions& substitutions) {
    for (Stmt& stmt : body) {
        substitute_stmt_types(stmt, substitutions);
    }
    return body;
}

} // namespace dudu
