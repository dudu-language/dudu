#include "dudu/lsp/language_server_semantic_token_expr.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"

#include <optional>
#include <string_view>

namespace dudu {
namespace {

constexpr int token_namespace = semantic_token_type::namespace_;
constexpr int token_type = semantic_token_type::type;
constexpr int token_class = semantic_token_type::class_;
constexpr int token_enum = semantic_token_type::enum_;
constexpr int token_function = semantic_token_type::function;
constexpr int token_method = semantic_token_type::method;
constexpr int token_variable = semantic_token_type::variable;
constexpr int token_property = semantic_token_type::property;
constexpr int token_enum_member = semantic_token_type::enum_member;
constexpr int token_macro = semantic_token_type::macro;
constexpr int token_number = semantic_token_type::number;
constexpr int token_string = semantic_token_type::string;

constexpr int mod_declaration = semantic_token_modifier::declaration;
constexpr int mod_readonly = semantic_token_modifier::readonly;
constexpr int mod_native = semantic_token_modifier::native;
constexpr int mod_unresolved = semantic_token_modifier::unresolved;

struct SemanticTokenShape {
    int type = 0;
    int modifiers = 0;
};

SourceLocation member_name_location(const Expr& expr) {
    if (expr.kind == ExprKind::Member && !expr.children.empty() &&
        expr.children.front().range.end.column > 0) {
        SourceLocation location = range_end_location(expr.children.front().range);
        ++location.column;
        return location;
    }
    return expr.location;
}

bool is_local_binding(const std::set<std::string>* local_bindings, const std::string& name) {
    return local_bindings != nullptr && local_bindings->contains(name);
}

TypeRef local_type_ref(const std::map<std::string, TypeRef>* local_types, const std::string& name) {
    if (local_types == nullptr) {
        return {};
    }
    const auto found = local_types->find(name);
    return found == local_types->end() ? TypeRef{} : found->second;
}

TypeRef obvious_expr_type(const Expr& expr, const DuduSemanticIndex& dudu_index,
                          const std::map<std::string, TypeRef>* local_types) {
    if (expr.kind == ExprKind::Name) {
        const TypeRef local_type = local_type_ref(local_types, expr.name);
        if (has_type_ref(local_type)) {
            return local_type;
        }
    }
    if ((expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
        has_expr_callee(expr)) {
        const Expr& callee = expr_callee(expr).front();
        if (callee.kind == ExprKind::Name && dudu_index.classes.contains(callee.name)) {
            return named_type_ref(callee.name, callee.location);
        }
    }
    return {};
}

TypeRef local_binding_type(const Stmt& stmt, const DuduSemanticIndex& dudu_index,
                           const std::map<std::string, TypeRef>& local_types) {
    if (has_stmt_type_ref(stmt)) {
        return stmt_type_ref(stmt);
    }
    return obvious_expr_type(stmt.value_expr, dudu_index, &local_types);
}

std::optional<SemanticTokenShape> member_token_shape_for_receiver(
    const Expr& receiver, std::string_view member, const DuduSemanticIndex& dudu_index,
    const NativeSemanticIndex* native_index, const std::map<std::string, TypeRef>* local_types) {
    const TypeRef receiver_type = obvious_expr_type(receiver, dudu_index, local_types);
    const std::string type_name = type_ref_head_name(receiver_type);
    if (type_name.empty()) {
        return std::nullopt;
    }
    const std::string key = type_name + "." + std::string(member);
    if (dudu_index.functions.contains(key)) {
        return SemanticTokenShape{.type = token_method};
    }
    if (dudu_index.values.contains(key)) {
        return SemanticTokenShape{.type = token_property};
    }
    if (dudu_index.enum_members.contains(key)) {
        return SemanticTokenShape{.type = token_enum_member};
    }
    if (native_index != nullptr && native_index->methods.contains(key)) {
        return SemanticTokenShape{.type = token_method, .modifiers = mod_native};
    }
    if (native_index != nullptr && native_index->values.contains(key)) {
        return SemanticTokenShape{.type = token_property, .modifiers = mod_native};
    }
    return std::nullopt;
}

std::optional<std::string> expr_path_key(const Expr& expr) {
    const std::optional<ExprPath> path = expr_path_from_expr(expr);
    return path ? std::optional<std::string>{render_expr_path(*path)} : std::nullopt;
}

void collect_call_callee_tokens(const Expr& expr, std::vector<SemanticToken>& tokens,
                                const DuduSemanticIndex& dudu_index,
                                const NativeSemanticIndex* native_index,
                                const std::set<std::string>* local_bindings,
                                const std::map<std::string, TypeRef>* local_types) {
    if (expr.kind == ExprKind::Name) {
        if (native_index != nullptr && native_index->macros.contains(expr.name)) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_macro);
        } else if (native_index != nullptr && native_index->functions.contains(expr.name)) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_function);
        } else if (dudu_index.classes.contains(expr.name)) {
            add_semantic_token(tokens, expr.location, expr.name, token_class);
        } else if (dudu_index.enums.contains(expr.name)) {
            add_semantic_token(tokens, expr.location, expr.name, token_enum);
        } else {
            const int modifiers = is_local_binding(local_bindings, expr.name) ? 0 : mod_unresolved;
            add_semantic_token(tokens, expr.location, expr.name, token_function, modifiers);
        }
        return;
    }
    if (expr.kind == ExprKind::Member) {
        for (const Expr& child : expr.children) {
            collect_semantic_expr_tokens(child, tokens, dudu_index, native_index, local_bindings,
                                         local_types);
        }
        const SourceLocation member_location = member_name_location(expr);
        const std::optional<std::string> path = expr_path_key(expr);
        if (path && dudu_index.functions.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_function);
        } else if (path && dudu_index.enum_members.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_enum_member, mod_readonly);
        } else if (path && dudu_index.values.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_variable, mod_readonly);
        } else if (native_index != nullptr && path && native_index->macros.contains(*path)) {
            add_native_semantic_token(tokens, member_location, expr.name, token_macro);
        } else if (native_index != nullptr && path && native_index->functions.contains(*path)) {
            add_native_semantic_token(tokens, member_location, expr.name, token_function);
        } else if (native_index != nullptr && path && native_index->methods.contains(*path)) {
            add_native_semantic_token(tokens, member_location, expr.name, token_method);
        } else if (!expr.children.empty()) {
            const std::optional<SemanticTokenShape> receiver_shape =
                member_token_shape_for_receiver(expr.children.front(), expr.name, dudu_index,
                                                native_index, local_types);
            add_semantic_token(tokens, member_location, expr.name,
                               receiver_shape ? receiver_shape->type : token_method,
                               receiver_shape ? receiver_shape->modifiers : mod_unresolved);
        } else {
            add_semantic_token(tokens, member_location, expr.name, token_method, mod_unresolved);
        }
        return;
    }
    collect_semantic_expr_tokens(expr, tokens, dudu_index, native_index, local_bindings,
                                 local_types);
}

} // namespace

void collect_semantic_type_tokens(const TypeRef& type, std::vector<SemanticToken>& tokens,
                                  const DuduSemanticIndex& dudu_index,
                                  const NativeSemanticIndex* native_index) {
    if (type.kind == TypeKind::Named || type.kind == TypeKind::Qualified ||
        type.kind == TypeKind::Template) {
        const std::string label_text = type_ref_head_name(type);
        const bool native_class =
            native_index != nullptr && (native_index->classes.contains(label_text) ||
                                        native_index->class_aliases.contains(label_text));
        const bool native_type =
            native_index != nullptr && native_index->types.contains(label_text);
        if (native_class) {
            add_native_semantic_token(tokens, type.location, label_text, token_class);
        } else if (native_type) {
            add_native_semantic_token(tokens, type.location, label_text, token_type);
        } else if (dudu_index.classes.contains(label_text)) {
            add_semantic_token(tokens, type.location, label_text, token_class);
        } else if (dudu_index.enums.contains(label_text)) {
            add_semantic_token(tokens, type.location, label_text, token_enum);
        } else {
            add_semantic_token(tokens, type.location, label_text, token_type);
        }
    }
    for (const TypeRef& child : type.children) {
        collect_semantic_type_tokens(child, tokens, dudu_index, native_index);
    }
}

void collect_semantic_expr_tokens(const Expr& expr, std::vector<SemanticToken>& tokens,
                                  const DuduSemanticIndex& dudu_index,
                                  const NativeSemanticIndex* native_index,
                                  const std::set<std::string>* local_bindings,
                                  const std::map<std::string, TypeRef>* local_types) {
    switch (expr.kind) {
    case ExprKind::Name:
        if (native_index != nullptr && (native_index->classes.contains(expr.name) ||
                                        native_index->class_aliases.contains(expr.name))) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_class);
        } else if (native_index != nullptr && native_index->types.contains(expr.name)) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_type);
        } else if (native_index != nullptr && native_index->macros.contains(expr.name)) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_macro);
        } else if (native_index != nullptr && native_index->functions.contains(expr.name)) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_function);
        } else if (native_index != nullptr && native_index->values.contains(expr.name)) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_variable,
                                      mod_readonly);
        } else if (native_index != nullptr && native_index->enum_members.contains(expr.name)) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_enum_member,
                                      mod_readonly);
        } else if (native_index != nullptr && native_index->namespaces.contains(expr.name)) {
            add_native_semantic_token(tokens, expr.location, expr.name, token_namespace);
        } else if (dudu_index.namespaces.contains(expr.name)) {
            add_semantic_token(tokens, expr.location, expr.name, token_namespace);
        } else if (dudu_index.types.contains(expr.name)) {
            add_semantic_token(tokens, expr.location, expr.name, token_type);
        } else if (dudu_index.classes.contains(expr.name)) {
            add_semantic_token(tokens, expr.location, expr.name, token_class);
        } else if (dudu_index.enums.contains(expr.name)) {
            add_semantic_token(tokens, expr.location, expr.name, token_enum);
        } else if (dudu_index.functions.contains(expr.name)) {
            add_semantic_token(tokens, expr.location, expr.name, token_function);
        } else if (dudu_index.values.contains(expr.name)) {
            add_semantic_token(tokens, expr.location, expr.name, token_variable, mod_readonly);
        } else {
            const int modifiers = is_local_binding(local_bindings, expr.name) ? 0 : mod_unresolved;
            add_semantic_token(tokens, expr.location, expr.name, token_variable, modifiers);
        }
        break;
    case ExprKind::Call:
    case ExprKind::TemplateCall:
        if (has_expr_callee(expr)) {
            collect_call_callee_tokens(expr_callee(expr).front(), tokens, dudu_index, native_index,
                                       local_bindings, local_types);
        } else {
            const int modifiers = is_local_binding(local_bindings, expr.name) ? 0 : mod_unresolved;
            add_semantic_token(tokens, expr.location, expr.name, token_function, modifiers);
        }
        break;
    case ExprKind::Member: {
        for (const Expr& child : expr.children) {
            collect_semantic_expr_tokens(child, tokens, dudu_index, native_index, local_bindings,
                                         local_types);
        }
        const SourceLocation member_location = member_name_location(expr);
        const std::optional<std::string> path = expr_path_key(expr);
        if (path && dudu_index.enum_members.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_enum_member, mod_readonly);
        } else if (path && dudu_index.types.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_type);
        } else if (path && dudu_index.classes.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_class);
        } else if (path && dudu_index.enums.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_enum);
        } else if (path && dudu_index.functions.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_function);
        } else if (path && dudu_index.values.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_variable, mod_readonly);
        } else if (native_index != nullptr && path &&
                   (native_index->classes.contains(*path) ||
                    native_index->class_aliases.contains(*path))) {
            add_native_semantic_token(tokens, member_location, expr.name, token_class);
        } else if (native_index != nullptr && path && native_index->types.contains(*path)) {
            add_native_semantic_token(tokens, member_location, expr.name, token_type);
        } else if (native_index != nullptr && path && native_index->values.contains(*path)) {
            add_native_semantic_token(tokens, member_location, expr.name, token_variable,
                                      mod_readonly);
        } else if (native_index != nullptr && path && native_index->enum_members.contains(*path)) {
            add_native_semantic_token(tokens, member_location, expr.name, token_enum_member,
                                      mod_readonly);
        } else if (!expr.children.empty()) {
            const std::optional<SemanticTokenShape> receiver_shape =
                member_token_shape_for_receiver(expr.children.front(), expr.name, dudu_index,
                                                native_index, local_types);
            add_semantic_token(tokens, member_location, expr.name,
                               receiver_shape ? receiver_shape->type : token_property,
                               receiver_shape ? receiver_shape->modifiers : mod_unresolved);
        } else {
            add_semantic_token(tokens, member_location, expr.name, token_property, mod_unresolved);
        }
        return;
    }
    case ExprKind::NamedArg:
        add_semantic_token(tokens, expr.location, expr.name, token_property);
        break;
    case ExprKind::IntLiteral:
    case ExprKind::FloatLiteral:
        if (!expr.value.empty()) {
            add_semantic_token(tokens, expr.location, expr.value, token_number);
        }
        break;
    case ExprKind::StringLiteral:
        add_semantic_token_range(tokens, expr.range, token_string);
        break;
    default:
        break;
    }
    if (expr.kind != ExprKind::Call && expr.kind != ExprKind::TemplateCall) {
        for (const Expr& callee : expr_callee(expr)) {
            collect_semantic_expr_tokens(callee, tokens, dudu_index, native_index, local_bindings,
                                         local_types);
        }
    }
    for (const Expr& child : expr.children) {
        collect_semantic_expr_tokens(child, tokens, dudu_index, native_index, local_bindings,
                                     local_types);
    }
    if (has_expr_template_type_args(expr)) {
        for (const TypeRef& arg : expr_template_type_args(expr)) {
            collect_semantic_type_tokens(arg, tokens, dudu_index, native_index);
        }
    } else {
        for (const Expr& arg : expr_template_args(expr)) {
            collect_semantic_expr_tokens(arg, tokens, dudu_index, native_index, local_bindings,
                                         local_types);
        }
    }
}

void collect_semantic_stmt_tokens(const std::vector<Stmt>& statements,
                                  std::vector<SemanticToken>& tokens,
                                  const DuduSemanticIndex& dudu_index,
                                  const NativeSemanticIndex* native_index,
                                  std::set<std::string>& local_bindings,
                                  std::map<std::string, TypeRef>& local_types) {
    for (const Stmt& stmt : statements) {
        if (stmt.kind == StmtKind::VarDecl) {
            add_semantic_token(tokens, stmt.location, stmt.name, token_variable, mod_declaration);
            local_bindings.insert(stmt.name);
            const TypeRef binding_type = local_binding_type(stmt, dudu_index, local_types);
            if (has_type_ref(binding_type)) {
                local_types[stmt.name] = binding_type;
            }
            collect_semantic_type_tokens(stmt_type_ref(stmt), tokens, dudu_index, native_index);
            collect_semantic_expr_tokens(stmt.value_expr, tokens, dudu_index, native_index,
                                         &local_bindings, &local_types);
        } else if (stmt.kind == StmtKind::Assign) {
            const Expr& target = stmt_target_expr(stmt);
            if (target.kind == ExprKind::Name && !local_bindings.contains(target.name)) {
                add_semantic_token(tokens, target.location, target.name, token_variable,
                                   mod_declaration);
                local_bindings.insert(target.name);
                const TypeRef binding_type = local_binding_type(stmt, dudu_index, local_types);
                if (has_type_ref(binding_type)) {
                    local_types[target.name] = binding_type;
                }
            } else {
                collect_semantic_expr_tokens(target, tokens, dudu_index, native_index,
                                             &local_bindings, &local_types);
            }
            collect_semantic_expr_tokens(stmt.value_expr, tokens, dudu_index, native_index,
                                         &local_bindings, &local_types);
        } else if (stmt.kind == StmtKind::CompoundAssign) {
            collect_semantic_expr_tokens(stmt_target_expr(stmt), tokens, dudu_index, native_index,
                                         &local_bindings, &local_types);
            collect_semantic_expr_tokens(stmt.value_expr, tokens, dudu_index, native_index,
                                         &local_bindings, &local_types);
        } else if (stmt.kind == StmtKind::Return || stmt.kind == StmtKind::Raise ||
                   stmt.kind == StmtKind::Delete) {
            collect_semantic_expr_tokens(stmt.value_expr, tokens, dudu_index, native_index,
                                         &local_bindings, &local_types);
        } else if (stmt.kind == StmtKind::If || stmt.kind == StmtKind::Elif ||
                   stmt.kind == StmtKind::While || stmt.kind == StmtKind::Assert ||
                   stmt.kind == StmtKind::DebugAssert) {
            collect_semantic_expr_tokens(stmt_condition_expr(stmt), tokens, dudu_index,
                                         native_index, &local_bindings, &local_types);
        } else if (stmt.kind == StmtKind::For) {
            add_semantic_token(tokens, stmt.location, stmt.name, token_variable, mod_declaration);
            local_bindings.insert(stmt.name);
            if (has_stmt_type_ref(stmt)) {
                local_types[stmt.name] = stmt_type_ref(stmt);
            }
            collect_semantic_type_tokens(stmt_type_ref(stmt), tokens, dudu_index, native_index);
            collect_semantic_expr_tokens(stmt_iterable_expr(stmt), tokens, dudu_index, native_index,
                                         &local_bindings, &local_types);
        } else if (stmt.kind == StmtKind::Expr) {
            collect_semantic_expr_tokens(stmt.expr, tokens, dudu_index, native_index,
                                         &local_bindings, &local_types);
        }
        collect_semantic_stmt_tokens(stmt.children, tokens, dudu_index, native_index,
                                     local_bindings, local_types);
    }
}

} // namespace dudu
