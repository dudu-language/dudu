#include "dudu/lsp/language_server_semantic_tokens.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_semantic_index.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

struct SemanticToken {
    int line = 0;
    int column = 0;
    int length = 0;
    int type = 0;
    int modifiers = 0;
};

struct SemanticTokenShape {
    int type = 0;
    int modifiers = 0;
};

constexpr int token_namespace = 0;
constexpr int token_type = 1;
constexpr int token_class = 2;
constexpr int token_enum = 3;
constexpr int token_function = 4;
constexpr int token_method = 5;
constexpr int token_variable = 6;
constexpr int token_parameter = 7;
constexpr int token_property = 8;
constexpr int token_enum_member = 9;
constexpr int token_macro = 10;
constexpr int token_number = 12;
constexpr int token_string = 13;

constexpr int mod_declaration = 1;
constexpr int mod_readonly = 4;
constexpr int mod_static = 8;
constexpr int mod_native = 16;
constexpr int mod_unresolved = 32;

void add_semantic_token(std::vector<SemanticToken>& tokens, const SourceLocation& loc,
                        std::string_view text, int type, int modifiers = 0) {
    if (text.empty() || loc.line <= 0 || loc.column <= 0) {
        return;
    }
    tokens.push_back({.line = loc.line - 1,
                      .column = loc.column - 1,
                      .length = static_cast<int>(text.size()),
                      .type = type,
                      .modifiers = modifiers});
}

void add_native_semantic_token(std::vector<SemanticToken>& tokens, const SourceLocation& loc,
                               std::string_view text, int type, int modifiers = 0) {
    add_semantic_token(tokens, loc, text, type, modifiers | mod_native);
}

void add_semantic_token_range(std::vector<SemanticToken>& tokens, const SourceRange& range,
                              int type, int modifiers = 0) {
    if (range.start.line <= 0 || range.start.column <= 0 || range.end.line != range.start.line ||
        range.end.column <= range.start.column) {
        return;
    }
    tokens.push_back({.line = range.start.line - 1,
                      .column = range.start.column - 1,
                      .length = range.end.column - range.start.column,
                      .type = type,
                      .modifiers = modifiers});
}

SourceLocation shifted_location(SourceLocation loc, int columns) {
    loc.column += columns;
    return loc;
}

SourceLocation import_bound_location(const ImportDecl& import) {
    SourceLocation loc = import.location;
    if (!import.alias.empty()) {
        if (import.kind == ImportKind::From) {
            loc.column +=
                static_cast<int>(std::string_view("from ").size() + import.module_path.size() +
                                 std::string_view(" import ").size() + import.imported_name.size() +
                                 std::string_view(" as ").size());
            return loc;
        }
        std::string_view prefix = "import ";
        if (import.kind == ImportKind::ForeignC) {
            prefix = "import c ";
        } else if (import.kind == ImportKind::ForeignCxx) {
            prefix = "import cxx ";
        } else if (import.kind == ImportKind::ForeignCpp) {
            prefix = "import cpp ";
        }
        loc.column += static_cast<int>(prefix.size() + import.module_path.size() +
                                       std::string_view(" as ").size());
        return loc;
    }
    if (import.kind == ImportKind::From) {
        loc.column +=
            static_cast<int>(std::string_view("from ").size() + import.module_path.size() +
                             std::string_view(" import ").size());
        return loc;
    }
    loc.column += static_cast<int>(std::string_view("import ").size());
    if (import.kind == ImportKind::ForeignC) {
        loc.column += static_cast<int>(std::string_view("c ").size());
    } else if (import.kind == ImportKind::ForeignCxx) {
        loc.column += static_cast<int>(std::string_view("cxx ").size());
    } else if (import.kind == ImportKind::ForeignCpp) {
        loc.column += static_cast<int>(std::string_view("cpp ").size());
    }
    return loc;
}

SourceLocation member_name_location(const Expr& expr) {
    if (expr.kind == ExprKind::Member && !expr.children.empty() &&
        expr.children.front().range.end.column > 0) {
        return shifted_location(range_end_location(expr.children.front().range), 1);
    }
    return expr.location;
}

void collect_type_tokens(const TypeRef& type, std::vector<SemanticToken>& tokens,
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
        collect_type_tokens(child, tokens, dudu_index, native_index);
    }
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

std::optional<SemanticTokenShape>
member_token_shape_for_receiver(const Expr& receiver, std::string_view member,
                                const DuduSemanticIndex& dudu_index,
                                const NativeSemanticIndex* native_index,
                                const std::map<std::string, TypeRef>* local_types, bool callee) {
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
    if (callee) {
        return std::nullopt;
    }
    return std::nullopt;
}

void collect_expr_tokens(const Expr& expr, std::vector<SemanticToken>& tokens,
                         const DuduSemanticIndex& dudu_index,
                         const NativeSemanticIndex* native_index,
                         const std::set<std::string>* local_bindings,
                         const std::map<std::string, TypeRef>* local_types);

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
            collect_expr_tokens(child, tokens, dudu_index, native_index, local_bindings,
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
            const std::optional<SemanticTokenShape> receiver_shape = member_token_shape_for_receiver(
                expr.children.front(), expr.name, dudu_index, native_index, local_types, true);
            add_semantic_token(tokens, member_location, expr.name,
                               receiver_shape ? receiver_shape->type : token_method,
                               receiver_shape ? receiver_shape->modifiers : mod_unresolved);
        } else if (path && dudu_index.enum_members.contains(*path)) {
            add_semantic_token(tokens, member_location, expr.name, token_enum_member, mod_readonly);
        } else {
            add_semantic_token(tokens, member_location, expr.name, token_method, mod_unresolved);
        }
        return;
    }
    collect_expr_tokens(expr, tokens, dudu_index, native_index, local_bindings, local_types);
}

void collect_expr_tokens(const Expr& expr, std::vector<SemanticToken>& tokens,
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
            collect_expr_tokens(child, tokens, dudu_index, native_index, local_bindings,
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
            const std::optional<SemanticTokenShape> receiver_shape = member_token_shape_for_receiver(
                expr.children.front(), expr.name, dudu_index, native_index, local_types, false);
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
            collect_expr_tokens(callee, tokens, dudu_index, native_index, local_bindings,
                                local_types);
        }
    }
    for (const Expr& child : expr.children) {
        collect_expr_tokens(child, tokens, dudu_index, native_index, local_bindings, local_types);
    }
    if (has_expr_template_type_args(expr)) {
        for (const TypeRef& arg : expr_template_type_args(expr)) {
            collect_type_tokens(arg, tokens, dudu_index, native_index);
        }
    } else {
        for (const Expr& arg : expr_template_args(expr)) {
            collect_expr_tokens(arg, tokens, dudu_index, native_index, local_bindings, local_types);
        }
    }
}

void collect_stmt_tokens(const std::vector<Stmt>& statements, std::vector<SemanticToken>& tokens,
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
            collect_type_tokens(stmt_type_ref(stmt), tokens, dudu_index, native_index);
            collect_expr_tokens(stmt.value_expr, tokens, dudu_index, native_index, &local_bindings,
                                &local_types);
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
                collect_expr_tokens(target, tokens, dudu_index, native_index, &local_bindings,
                                    &local_types);
            }
            collect_expr_tokens(stmt.value_expr, tokens, dudu_index, native_index, &local_bindings,
                                &local_types);
        } else if (stmt.kind == StmtKind::CompoundAssign) {
            collect_expr_tokens(stmt_target_expr(stmt), tokens, dudu_index, native_index,
                                &local_bindings, &local_types);
            collect_expr_tokens(stmt.value_expr, tokens, dudu_index, native_index, &local_bindings,
                                &local_types);
        } else if (stmt.kind == StmtKind::Return || stmt.kind == StmtKind::Raise ||
                   stmt.kind == StmtKind::Delete) {
            collect_expr_tokens(stmt.value_expr, tokens, dudu_index, native_index, &local_bindings,
                                &local_types);
        } else if (stmt.kind == StmtKind::If || stmt.kind == StmtKind::Elif ||
                   stmt.kind == StmtKind::While || stmt.kind == StmtKind::Assert ||
                   stmt.kind == StmtKind::DebugAssert) {
            collect_expr_tokens(stmt_condition_expr(stmt), tokens, dudu_index, native_index,
                                &local_bindings, &local_types);
        } else if (stmt.kind == StmtKind::For) {
            add_semantic_token(tokens, stmt.location, stmt.name, token_variable, mod_declaration);
            local_bindings.insert(stmt.name);
            if (has_stmt_type_ref(stmt)) {
                local_types[stmt.name] = stmt_type_ref(stmt);
            }
            collect_type_tokens(stmt_type_ref(stmt), tokens, dudu_index, native_index);
            collect_expr_tokens(stmt_iterable_expr(stmt), tokens, dudu_index, native_index,
                                &local_bindings, &local_types);
        } else if (stmt.kind == StmtKind::Expr) {
            collect_expr_tokens(stmt.expr, tokens, dudu_index, native_index, &local_bindings,
                                &local_types);
        }
        collect_stmt_tokens(stmt.children, tokens, dudu_index, native_index, local_bindings,
                            local_types);
    }
}

void collect_semantic_tokens(const ModuleAst& module, std::vector<SemanticToken>& tokens,
                             const NativeSemanticIndex* native_index,
                             const DuduSemanticIndex& dudu_index) {
    for (const ImportDecl& import : module.imports) {
        const bool native_import = import.kind == ImportKind::ForeignC ||
                                   import.kind == ImportKind::ForeignCxx ||
                                   import.kind == ImportKind::ForeignCpp;
        const std::string bound = bound_import_name(import);
        int import_token_type = token_namespace;
        int import_modifiers = mod_declaration | (native_import ? mod_native : 0);
        if (import.kind == ImportKind::From && !native_import) {
            if (dudu_index.classes.contains(bound)) {
                import_token_type = token_class;
            } else if (dudu_index.enums.contains(bound)) {
                import_token_type = token_enum;
            } else if (dudu_index.enum_members.contains(bound)) {
                import_token_type = token_enum_member;
                import_modifiers |= mod_readonly;
            } else if (dudu_index.functions.contains(bound)) {
                import_token_type = token_function;
            } else if (dudu_index.types.contains(bound)) {
                import_token_type = token_type;
            } else if (dudu_index.values.contains(bound)) {
                import_token_type = token_variable;
                import_modifiers |= mod_readonly;
            }
        }
        add_semantic_token(tokens, import_bound_location(import), bound, import_token_type,
                           import_modifiers);
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        add_semantic_token(tokens, shifted_location(alias.location, 5), alias.name, token_type,
                           mod_declaration);
        collect_type_tokens(alias.type_ref, tokens, dudu_index, native_index);
    }
    for (const EnumDecl& en : module.enums) {
        add_semantic_token(tokens, en.location, en.name, token_enum, mod_declaration);
        collect_type_tokens(en.underlying_type_ref, tokens, dudu_index, native_index);
        for (const EnumValueDecl& value : en.values) {
            add_semantic_token(tokens, value.location, value.name, token_enum_member,
                               mod_declaration);
            collect_expr_tokens(value.value_expr, tokens, dudu_index, native_index, nullptr,
                                nullptr);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        add_semantic_token(tokens, klass.location, klass.name, token_class, mod_declaration);
        for (const FieldDecl& field : klass.fields) {
            add_semantic_token(tokens, field.location, field.name, token_property, mod_declaration);
            collect_type_tokens(field.type_ref, tokens, dudu_index, native_index);
            collect_expr_tokens(field.value_expr, tokens, dudu_index, native_index, nullptr,
                                nullptr);
        }
        for (const ConstDecl& constant : klass.constants) {
            add_semantic_token(tokens, constant.location, constant.name, token_property,
                               mod_declaration | mod_readonly);
            collect_type_tokens(constant.type_ref, tokens, dudu_index, native_index);
            collect_expr_tokens(constant.value_expr, tokens, dudu_index, native_index, nullptr,
                                nullptr);
        }
        for (const ConstDecl& field : klass.static_fields) {
            add_semantic_token(tokens, field.location, field.name, token_property,
                               mod_declaration | mod_static);
            collect_type_tokens(field.type_ref, tokens, dudu_index, native_index);
            collect_expr_tokens(field.value_expr, tokens, dudu_index, native_index, nullptr,
                                nullptr);
        }
        for (const FunctionDecl& method : klass.methods) {
            add_semantic_token(tokens, method.location, method.name, token_method, mod_declaration);
            std::set<std::string> local_bindings;
            std::map<std::string, TypeRef> local_types;
            for (const ParamDecl& param : method.params) {
                add_semantic_token(tokens, param.location, param.name, token_parameter,
                                   mod_declaration);
                local_bindings.insert(param.name);
                if (param.name != "self") {
                    collect_type_tokens(param.type_ref, tokens, dudu_index, native_index);
                    if (has_type_ref(param.type_ref)) {
                        local_types[param.name] = param.type_ref;
                    }
                } else {
                    local_types[param.name] = named_type_ref(klass.name, param.location);
                }
            }
            collect_type_tokens(method.return_type_ref, tokens, dudu_index, native_index);
            collect_stmt_tokens(method.statements, tokens, dudu_index, native_index, local_bindings,
                                local_types);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        add_semantic_token(tokens, constant.location, constant.name, token_variable,
                           mod_declaration | mod_readonly);
        collect_type_tokens(constant.type_ref, tokens, dudu_index, native_index);
        collect_expr_tokens(constant.value_expr, tokens, dudu_index, native_index, nullptr,
                            nullptr);
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        collect_expr_tokens(assertion.expression_expr, tokens, dudu_index, native_index, nullptr,
                            nullptr);
    }
    for (const FunctionDecl& fn : module.functions) {
        add_semantic_token(tokens, fn.location, fn.name, token_function, mod_declaration);
        std::set<std::string> local_bindings;
        std::map<std::string, TypeRef> local_types;
        for (const ParamDecl& param : fn.params) {
            add_semantic_token(tokens, param.location, param.name, token_parameter,
                               mod_declaration);
            local_bindings.insert(param.name);
            if (has_type_ref(param.type_ref)) {
                local_types[param.name] = param.type_ref;
            }
            collect_type_tokens(param.type_ref, tokens, dudu_index, native_index);
        }
        collect_type_tokens(fn.return_type_ref, tokens, dudu_index, native_index);
        collect_stmt_tokens(fn.statements, tokens, dudu_index, native_index, local_bindings,
                            local_types);
    }
}

std::string encode_semantic_tokens(std::vector<SemanticToken> tokens) {
    std::sort(tokens.begin(), tokens.end(),
              [](const SemanticToken& left, const SemanticToken& right) {
                  if (left.line != right.line) {
                      return left.line < right.line;
                  }
                  if (left.column != right.column) {
                      return left.column < right.column;
                  }
                  if (left.length != right.length) {
                      return left.length < right.length;
                  }
                  return left.type < right.type;
              });

    std::ostringstream out;
    out << "{\"data\":[";
    int previous_line = 0;
    int previous_column = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const SemanticToken& token = tokens[i];
        if (i > 0) {
            out << ',';
        }
        const int delta_line = i == 0 ? token.line : token.line - previous_line;
        const int delta_column = delta_line == 0 ? token.column - previous_column : token.column;
        out << delta_line << ',' << delta_column << ',' << token.length << ',' << token.type << ','
            << token.modifiers;
        previous_line = token.line;
        previous_column = token.column;
    }
    out << "]}";
    return out.str();
}

} // namespace

std::string semantic_tokens_json(const ModuleAst& module, const ModuleAst& native_symbols) {
    std::vector<SemanticToken> tokens;
    const NativeSemanticIndex native_index = native_semantic_index(native_symbols);
    collect_semantic_tokens(module, tokens, &native_index, dudu_semantic_index(module));
    return encode_semantic_tokens(std::move(tokens));
}

std::string semantic_tokens_json(const ModuleAst& module) {
    return semantic_tokens_json(module, {});
}

std::string semantic_tokens_json(const ProjectIndex& index, const std::filesystem::path& path,
                                 const ProjectIndex& native_index) {
    const ModuleAst& current = index.visible_unit_for_path(path);
    const NativeSemanticIndex native_symbols =
        native_semantic_index(native_index.visible_unit_for_path(path));
    std::vector<SemanticToken> tokens;
    collect_semantic_tokens(current, tokens, &native_symbols, dudu_semantic_index(index, current));
    return encode_semantic_tokens(std::move(tokens));
}

} // namespace dudu
