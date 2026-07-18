#include "dudu/lsp/language_server_semantic_tokens.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_semantic_index.hpp"
#include "dudu/lsp/language_server_semantic_token_expr.hpp"
#include "dudu/lsp/language_server_semantic_token_wire.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace dudu {
namespace {

constexpr int token_namespace = semantic_token_type::namespace_;
constexpr int token_type = semantic_token_type::type;
constexpr int token_class = semantic_token_type::class_;
constexpr int token_enum = semantic_token_type::enum_;
constexpr int token_function = semantic_token_type::function;
constexpr int token_method = semantic_token_type::method;
constexpr int token_variable = semantic_token_type::variable;
constexpr int token_parameter = semantic_token_type::parameter;
constexpr int token_property = semantic_token_type::property;
constexpr int token_enum_member = semantic_token_type::enum_member;
constexpr int token_macro = semantic_token_type::macro;
constexpr int token_string = semantic_token_type::string;

constexpr int mod_declaration = semantic_token_modifier::declaration;
constexpr int mod_readonly = semantic_token_modifier::readonly;
constexpr int mod_static = semantic_token_modifier::static_;
constexpr int mod_native = semantic_token_modifier::native;

SourceLocation shifted_location(SourceLocation location, int columns) {
    location.column += columns;
    return location;
}

SourceLocation import_bound_location(const ImportDecl& import) {
    if (!import.alias.empty()) {
        return import.alias_range.start;
    }
    if (import.kind == ImportKind::From) {
        return import.imported_name_range.start;
    }
    return import.module_range.start;
}

std::optional<std::string> decorator_path_text(const Decorator& decorator) {
    const Expr& expr = decorator.expr;
    if ((expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
        has_expr_callee(expr)) {
        if (const std::optional<ExprPath> path = expr_path_from_expr(expr_callee(expr).front())) {
            return render_expr_path(*path);
        }
        return std::nullopt;
    }
    if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
        return render_expr_path(*path);
    }
    return std::nullopt;
}

void collect_decorator_tokens(const std::vector<Decorator>& decorators,
                              std::vector<SemanticToken>& tokens,
                              const DuduSemanticIndex& dudu_index,
                              const std::set<std::string>& resolved_macros) {
    for (const Decorator& decorator : decorators) {
        const std::optional<std::string> name = decorator_path_text(decorator);
        if (!name) {
            continue;
        }
        const int type = dudu_index.functions.contains(*name) && !resolved_macros.contains(*name)
                             ? token_function
                             : token_macro;
        add_semantic_token(tokens, decorator.location, "@" + *name, type,
                           type == token_macro ? mod_readonly : 0);
        if (decorator.expr.kind != ExprKind::Call) {
            continue;
        }
        if (*name == "derive") {
            for (const Expr& argument : decorator.expr.children) {
                const std::optional<ExprPath> path = expr_path_from_expr(argument);
                if (!path || path->segments.empty()) {
                    continue;
                }
                for (size_t i = 0; i < path->segments.size(); ++i) {
                    const ExprPathSegment& segment = path->segments[i];
                    add_semantic_token(tokens, segment.location, segment.text,
                                       i + 1 == path->segments.size() ? token_macro
                                                                      : token_namespace,
                                       i + 1 == path->segments.size() ? mod_readonly : 0);
                }
            }
        } else if (resolved_macros.contains(*name)) {
            for (const Expr& argument : decorator.expr.children) {
                if (argument.kind == ExprKind::NamedArg) {
                    add_semantic_token(tokens, argument.location, argument.name, token_parameter);
                }
            }
        }
    }
}

void collect_function_tokens(const FunctionDecl& function, int declaration_type,
                             const std::optional<std::string>& self_type,
                             std::vector<SemanticToken>& tokens,
                             const DuduSemanticIndex& dudu_index,
                             const NativeSemanticIndex* native_index,
                             const std::set<std::string>& resolved_macros) {
    collect_decorator_tokens(function.decorators, tokens, dudu_index, resolved_macros);
    add_semantic_token(tokens, function.location, function.name, declaration_type, mod_declaration);
    const std::set<std::string> value_params = generic_value_params_for_function(function);
    for (const GenericParamDecl& param : function.generic_param_decls) {
        add_semantic_token(tokens, param.location, param.name,
                           value_params.contains(param.name) ? token_variable : token_type,
                           mod_declaration | mod_readonly);
    }

    std::set<std::string> local_bindings;
    std::map<std::string, TypeRef> local_types;
    for (const ParamDecl& param : function.params) {
        add_semantic_token(tokens, param.location, param.name, token_parameter, mod_declaration);
        local_bindings.insert(param.name);
        if (self_type && param.name == "self") {
            local_types[param.name] = named_type_ref(*self_type, param.location);
            continue;
        }
        collect_semantic_type_tokens(param.type_ref, tokens, dudu_index, native_index);
        if (has_type_ref(param.type_ref)) {
            local_types[param.name] = param.type_ref;
        }
    }
    collect_semantic_type_tokens(function.return_type_ref, tokens, dudu_index, native_index);
    collect_semantic_stmt_tokens(function.statements, tokens, dudu_index, native_index,
                                 local_bindings, local_types);
}

void collect_semantic_tokens(const ModuleAst& module, std::vector<SemanticToken>& tokens,
                             const NativeSemanticIndex* native_index,
                             const DuduSemanticIndex& dudu_index) {
    for (const ImportDecl& import : module.imports) {
        const bool native_import = import.kind == ImportKind::ForeignC ||
                                   import.kind == ImportKind::ForeignCxx ||
                                   import.kind == ImportKind::ForeignCpp;
        if (native_import) {
            add_semantic_token_range(tokens, import.module_range, token_string, mod_native);
            if (!import.alias.empty()) {
                add_semantic_token_range(tokens, import.alias_range, token_namespace,
                                         mod_declaration | mod_native);
            }
            continue;
        }

        const std::string bound = bound_import_name(import);
        int type = token_namespace;
        int modifiers = mod_declaration;
        if (import.kind == ImportKind::From) {
            if (module.resolved_macro_decorators.contains(bound)) {
                type = token_macro;
                modifiers |= mod_readonly;
            } else if (dudu_index.classes.contains(bound)) {
                type = token_class;
            } else if (dudu_index.enums.contains(bound)) {
                type = token_enum;
            } else if (dudu_index.enum_members.contains(bound)) {
                type = token_enum_member;
                modifiers |= mod_readonly;
            } else if (dudu_index.functions.contains(bound)) {
                type = token_function;
            } else if (dudu_index.types.contains(bound)) {
                type = token_type;
            } else if (dudu_index.values.contains(bound)) {
                type = token_variable;
                modifiers |= mod_readonly;
            }
        }
        add_semantic_token(tokens, import_bound_location(import), bound, type, modifiers);
    }

    for (const TypeAliasDecl& alias : module.aliases) {
        add_semantic_token(tokens, shifted_location(alias.location, 5), alias.name, token_type,
                           mod_declaration);
        collect_semantic_type_tokens(alias.type_ref, tokens, dudu_index, native_index);
    }

    for (const EnumDecl& en : module.enums) {
        collect_decorator_tokens(en.decorators, tokens, dudu_index,
                                 module.resolved_macro_decorators);
        add_semantic_token(tokens, en.location, en.name, token_enum, mod_declaration);
        collect_semantic_type_tokens(en.underlying_type_ref, tokens, dudu_index, native_index);
        for (const EnumValueDecl& value : en.values) {
            collect_decorator_tokens(value.decorators, tokens, dudu_index,
                                     module.resolved_macro_decorators);
            add_semantic_token(tokens, value.location, value.name, token_enum_member,
                               mod_declaration);
            collect_semantic_expr_tokens(value.value_expr, tokens, dudu_index, native_index);
        }
    }

    for (const ClassDecl& klass : module.classes) {
        collect_decorator_tokens(klass.decorators, tokens, dudu_index,
                                 module.resolved_macro_decorators);
        add_semantic_token(tokens, klass.location, klass.name, token_class, mod_declaration);
        const std::set<std::string> class_value_params = generic_value_params_for_class(klass);
        for (const GenericParamDecl& param : klass.generic_param_decls) {
            add_semantic_token(tokens, param.location, param.name,
                               class_value_params.contains(param.name) ? token_variable
                                                                       : token_type,
                               mod_declaration | mod_readonly);
        }
        for (const FieldDecl& field : klass.fields) {
            collect_decorator_tokens(field.decorators, tokens, dudu_index,
                                     module.resolved_macro_decorators);
            add_semantic_token(tokens, field.location, field.name, token_property, mod_declaration);
            collect_semantic_type_tokens(field.type_ref, tokens, dudu_index, native_index);
            collect_semantic_expr_tokens(field.value_expr, tokens, dudu_index, native_index);
        }
        for (const ConstDecl& constant : klass.constants) {
            collect_decorator_tokens(constant.decorators, tokens, dudu_index,
                                     module.resolved_macro_decorators);
            add_semantic_token(tokens, constant.location, constant.name, token_property,
                               mod_declaration | mod_readonly);
            collect_semantic_type_tokens(constant.type_ref, tokens, dudu_index, native_index);
            collect_semantic_expr_tokens(constant.value_expr, tokens, dudu_index, native_index);
        }
        for (const ConstDecl& field : klass.static_fields) {
            collect_decorator_tokens(field.decorators, tokens, dudu_index,
                                     module.resolved_macro_decorators);
            add_semantic_token(tokens, field.location, field.name, token_property,
                               mod_declaration | mod_static);
            collect_semantic_type_tokens(field.type_ref, tokens, dudu_index, native_index);
            collect_semantic_expr_tokens(field.value_expr, tokens, dudu_index, native_index);
        }
        for (const FunctionDecl& method : klass.methods) {
            collect_function_tokens(method, token_method, klass.name, tokens, dudu_index,
                                    native_index, module.resolved_macro_decorators);
        }
    }

    for (const ConstDecl& constant : module.constants) {
        collect_decorator_tokens(constant.decorators, tokens, dudu_index,
                                 module.resolved_macro_decorators);
        add_semantic_token(tokens, constant.location, constant.name, token_variable,
                           mod_declaration | mod_readonly);
        collect_semantic_type_tokens(constant.type_ref, tokens, dudu_index, native_index);
        collect_semantic_expr_tokens(constant.value_expr, tokens, dudu_index, native_index);
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        collect_semantic_expr_tokens(assertion.expression_expr, tokens, dudu_index, native_index);
    }
    for (const FunctionDecl& function : module.functions) {
        collect_function_tokens(function, token_function, std::nullopt, tokens, dudu_index,
                                native_index, module.resolved_macro_decorators);
    }
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
