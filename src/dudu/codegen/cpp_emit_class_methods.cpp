#include "dudu/codegen/cpp_emit_class_methods.hpp"

#include "dudu/codegen/cpp_emit_declaration_support.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_emit.hpp"
#include "dudu/codegen/cpp_stmt_emit_support.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/core/naming.hpp"
#include "dudu/sema/sema_body_substitution.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_inheritance.hpp"

#include <set>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

std::string class_lookup_name(const Symbols& symbols, TypeRef type) {
    type = resolve_alias_ref(symbols, std::move(type));
    return type_ref_head_name(type);
}

std::string function_decorator_arg(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (const std::optional<std::string> arg =
                decorator_first_string_literal_arg(decorator, name)) {
            return *arg;
        }
    }
    return {};
}

std::string method_emit_name(const std::string& source_class_name, const FunctionDecl& method,
                             const CppEmitOptions& options) {
    const std::string op = function_decorator_arg(method, "operator");
    if (!op.empty()) {
        if (op == "[]" || op == "[]=") {
            return method.name;
        }
        return op == "bool" ? "operator bool" : "operator" + op;
    }
    if (!cpp_reserved_identifier(method.name)) {
        return method.name;
    }
    return emitted_reserved_member_name(source_class_name, method.name, options);
}

bool variadic_index_assignment_method(const FunctionDecl& method) {
    if (function_decorator_arg(method, "operator") != "[]=" || method.params.empty()) {
        return false;
    }
    for (size_t i = 0; i < method.params.size(); ++i) {
        if (method.params[i].variadic && i + 1 < method.params.size()) {
            return true;
        }
    }
    return false;
}

std::vector<size_t> emitted_method_param_order(const FunctionDecl& method, size_t first_param) {
    std::vector<size_t> order;
    if (variadic_index_assignment_method(method) && method.params.size() > first_param) {
        order.push_back(method.params.size() - 1);
        for (size_t i = first_param; i + 1 < method.params.size(); ++i) {
            order.push_back(i);
        }
        return order;
    }
    for (size_t i = first_param; i < method.params.size(); ++i) {
        order.push_back(i);
    }
    return order;
}

const Expr* super_init_expr(const FunctionDecl& method) {
    if (!is_constructor_method(method) || method.statements.empty()) {
        return nullptr;
    }
    const Stmt& first = method.statements.front();
    if (first.kind == StmtKind::Expr && is_member_callee(first.expr, "super", "init")) {
        return &first.expr;
    }
    return nullptr;
}

const BaseClassDecl* super_init_base_decl(const Symbols& symbols, const std::string& class_name) {
    const auto klass = symbols.classes.find(class_name);
    if (klass == symbols.classes.end() || klass->second->base_class_refs.empty()) {
        return nullptr;
    }
    if (klass->second->base_class_refs.size() == 1) {
        return &klass->second->base_class_refs.front();
    }
    const BaseClassDecl* storage_base = nullptr;
    for (const BaseClassDecl& base : klass->second->base_class_refs) {
        if (!class_type_has_instance_storage(symbols, base.type_ref)) {
            continue;
        }
        if (storage_base != nullptr) {
            return nullptr;
        }
        storage_base = &base;
    }
    return storage_base;
}

std::string join_lowered_args(const std::vector<Expr>& args,
                              const std::vector<std::string>& aliases,
                              const CppLocalContext& locals) {
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_expr_ast(args[i], aliases, locals);
    }
    return out.str();
}

bool method_is_instance(const FunctionDecl& method) {
    return !method.params.empty() && method.params.front().name == "self";
}

std::map<std::string, TypeRef> self_type_substitution(const std::string& class_name,
                                                      SourceLocation location) {
    return {{"Self", named_type_ref(class_name, location)}};
}

TypeRef method_type_for_emit(const TypeRef& type, const std::string& class_name,
                             SourceLocation location) {
    return substitute_type_ref(type, self_type_substitution(class_name, location));
}

TypeRef method_return_type_for_emit(const FunctionDecl& method, const std::string& class_name) {
    return method_type_for_emit(function_return_type_ref(method), class_name, method.location);
}

bool self_receiver_is_const(const FunctionDecl& method, const std::string& class_name) {
    if (!method_is_instance(method)) {
        return false;
    }
    const TypeRef type = method_type_for_emit(method.params.front().type_ref, class_name,
                                              method.params.front().location);
    return type.kind == TypeKind::Reference && type.children.size() == 1 &&
           type.children.front().kind == TypeKind::Const;
}

bool has_drop_method(const ClassDecl& klass) {
    for (const FunctionDecl& method : klass.methods) {
        if (is_destructor_method(method)) {
            return true;
        }
    }
    return false;
}

bool class_is_polymorphic(const Symbols& symbols, const ClassDecl& klass,
                          std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return false;
    }
    for (const FunctionDecl& method : klass.methods) {
        if (method_is_instance(method) &&
            (has_decorator(method, "virtual") || has_decorator(method, "abstract"))) {
            return true;
        }
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        const auto parent = symbols.classes.find(class_lookup_name(symbols, base_decl.type_ref));
        if (parent != symbols.classes.end() &&
            class_is_polymorphic(symbols, *parent->second, seen)) {
            return true;
        }
    }
    return false;
}

bool class_is_polymorphic(const Symbols& symbols, const ClassDecl& klass) {
    std::set<std::string> seen;
    return class_is_polymorphic(symbols, klass, seen);
}

std::string_view class_section_for_method(Visibility visibility) {
    return visibility == Visibility::Private ? "private" : "public";
}

bool inline_method_body(const ClassDecl& klass, const FunctionDecl& method) {
    return !generic_cpp_params_for_class(klass).empty() ||
           !cpp_emit_template_params_for_function(method).empty();
}

void emit_method(std::ostringstream& out, const std::string& class_name,
                 const std::string& source_class_name, const FunctionDecl& method,
                 const std::vector<std::string>& aliases,
                 const std::map<std::string, TypeRef>& function_returns, const Symbols& symbols,
                 const CppEmitOptions& options, bool declaration_only = false,
                 bool qualified_definition = false) {
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    const std::string_view prefix = qualified_definition ? "" : "    ";
    emit_cpp_template_parameters(out, cpp_emit_template_params_for_function(method),
                                 generic_cpp_value_params_for_function(method), prefix);
    if (is_constructor_method(method)) {
        out << prefix << (qualified_definition ? class_name + "::" : std::string{}) << class_name
            << '(';
    } else if (is_destructor_method(method)) {
        const auto klass = symbols.classes.find(source_class_name);
        if (!qualified_definition && klass != symbols.classes.end() &&
            class_is_polymorphic(symbols, *klass->second)) {
            out << "    virtual ";
        } else {
            out << prefix;
        }
        out << (qualified_definition ? class_name + "::" : std::string{}) << "~" << class_name
            << '(';
    } else {
        out << prefix;
        if (!qualified_definition && first_param == 0) {
            out << "static ";
        }
        if (!qualified_definition && first_param == 1 &&
            (has_decorator(method, "virtual") || has_decorator(method, "abstract"))) {
            out << "virtual ";
        }
        const std::string lowered_name = method_emit_name(source_class_name, method, options);
        if (lowered_name == "operator bool") {
            if (!qualified_definition) {
                out << "explicit ";
            }
            out << (qualified_definition ? class_name + "::" : std::string{}) << lowered_name
                << '(';
        } else {
            out << lower_cpp_type(method_return_type_for_emit(method, class_name), aliases, options)
                << ' ' << (qualified_definition ? class_name + "::" : std::string{}) << lowered_name
                << '(';
        }
    }
    const std::vector<size_t> param_order = emitted_method_param_order(method, first_param);
    for (size_t i = 0; i < param_order.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        const ParamDecl& param = method.params[param_order[i]];
        if (cpp_emit_concrete_variadic_param(method, param)) {
            out << cpp_emit_concrete_variadic_pack_name(param) << "... " << param.name;
        } else {
            out << lower_cpp_type(method_type_for_emit(param.type_ref, class_name, param.location),
                                  aliases, options)
                << (param.variadic ? "... " : " ") << param.name;
        }
    }
    CppLocalContext locals;
    std::map<std::string, TypeRef> local_type_refs;
    locals.current_class = class_name;
    local_type_refs["class"] = named_type_ref(class_name, method.location);
    const auto klass = symbols.classes.find(source_class_name);
    if (klass != symbols.classes.end()) {
        const std::set<std::string> value_params = generic_value_params_for_class(*klass->second);
        for (const std::string& param : klass->second->generic_params) {
            const std::string name = generic_param_base_name(param);
            if (!value_params.contains(name)) {
                locals.bind_type(name);
            }
        }
    }
    const std::set<std::string> method_value_params = generic_value_params_for_function(method);
    for (const std::string& param : method.generic_params) {
        const std::string name = generic_param_base_name(param);
        if (!method_value_params.contains(name)) {
            locals.bind_type(name);
        }
    }
    if (klass != symbols.classes.end() && klass->second->base_class_refs.size() == 1) {
        locals.super_class =
            lower_cpp_type(klass->second->base_class_refs.front().type_ref, aliases, options);
        local_type_refs["super"] = klass->second->base_class_refs.front().type_ref;
    }
    if (first_param == 1) {
        locals.bind(method.params.front().name);
        local_type_refs[method.params.front().name] = method_type_for_emit(
            method.params.front().type_ref, source_class_name, method.params.front().location);
    }
    for (size_t i = first_param; i < method.params.size(); ++i) {
        locals.bind(method.params[i].name);
        local_type_refs[method.params[i].name] = method_type_for_emit(
            method.params[i].type_ref, source_class_name, method.params[i].location);
    }
    out << ")";
    if (!is_constructor_method(method) && !is_destructor_method(method) &&
        self_receiver_is_const(method, class_name)) {
        out << " const";
    }
    if (!qualified_definition && has_decorator(method, "override")) {
        out << " override";
    }
    if (has_decorator(method, "abstract")) {
        out << " = 0;\n";
        return;
    }
    if (declaration_only) {
        out << ";\n";
        return;
    }
    if (is_constructor_method(method)) {
        if (const Expr* super_init = super_init_expr(method)) {
            if (const BaseClassDecl* base = super_init_base_decl(symbols, class_name)) {
                out << " : " << lower_cpp_type(base->type_ref, aliases, options) << "("
                    << join_lowered_args(super_init->children, aliases, locals) << ")";
            }
        }
    }
    out << " {\n";
    if (first_param == 1) {
        out << "        auto& self = *this;\n";
    }
    if (is_constructor_method(method) && super_init_expr(method) != nullptr) {
        std::vector<Stmt> body(method.statements.begin() + 1, method.statements.end());
        body = substitute_body_types(std::move(body),
                                     self_type_substitution(class_name, method.location));
        emit_block(out, body, 2, aliases, locals, local_type_refs,
                   method_return_type_for_emit(method, class_name), function_returns, &symbols,
                   options);
    } else {
        const std::vector<Stmt> body = substitute_body_types(
            method.statements, self_type_substitution(class_name, method.location));
        emit_block(out, body, 2, aliases, locals, local_type_refs,
                   method_return_type_for_emit(method, class_name), function_returns, &symbols,
                   options);
    }
    out << "    }\n";
}

} // namespace

void emit_class_method_members(std::ostringstream& out, const ClassDecl& klass,
                               const std::string& class_name,
                               const std::vector<std::string>& aliases,
                               const std::map<std::string, TypeRef>& function_returns,
                               const Symbols& symbols, const CppEmitOptions& options) {
    if (class_is_polymorphic(symbols, klass) && !has_drop_method(klass)) {
        out << "    virtual ~" << class_name << "() = default;\n";
    }
    std::string_view current_section = "public";
    for (const FunctionDecl& method : klass.methods) {
        const std::string_view method_section = class_section_for_method(method.visibility);
        if (method_section != current_section) {
            out << method_section << ":\n";
            current_section = method_section;
        }
        emit_method(out, class_name, klass.name, method, aliases, function_returns, symbols,
                    options, !inline_method_body(klass, method));
    }
}

void emit_out_of_line_class_methods(std::ostringstream& out, const ClassDecl& klass,
                                    const std::string& class_name,
                                    const std::vector<std::string>& aliases,
                                    const std::map<std::string, TypeRef>& function_returns,
                                    const Symbols& symbols, const CppEmitOptions& options) {
    if (!generic_cpp_params_for_class(klass).empty()) {
        return;
    }
    bool emitted = false;
    for (const FunctionDecl& method : klass.methods) {
        if (inline_method_body(klass, method) || has_decorator(method, "abstract")) {
            continue;
        }
        emit_method(out, class_name, klass.name, method, aliases, function_returns, symbols,
                    options, false, true);
        out << '\n';
        emitted = true;
    }
    if (emitted) {
        out << '\n';
    }
}

} // namespace dudu
