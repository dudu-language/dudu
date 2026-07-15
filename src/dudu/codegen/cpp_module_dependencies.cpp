#include "dudu/codegen/cpp_module_dependencies.hpp"

#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <set>
#include <string>

namespace dudu {
namespace {

using Names = std::set<std::string>;

void collect_type_names(const TypeRef& type, Names& names) {
    if (!type.name.empty()) {
        names.insert(type.name.str());
    }
    for (const TypeRef& child : type.children) {
        collect_type_names(child, names);
    }
}

void collect_expr_names(const Expr& expr, Names& names) {
    if (!expr.name.empty()) {
        names.insert(expr.name.str());
    }
    if (expr.type_ref) {
        collect_type_names(*expr.type_ref, names);
    }
    if (expr.callee) {
        for (const Expr& child : *expr.callee) {
            collect_expr_names(child, names);
        }
    }
    if (expr.template_args) {
        for (const Expr& child : *expr.template_args) {
            collect_expr_names(child, names);
        }
    }
    if (expr.template_type_args) {
        for (const TypeRef& type : *expr.template_type_args) {
            collect_type_names(type, names);
        }
    }
    for (const Expr& child : expr.children) {
        collect_expr_names(child, names);
    }
}

void collect_stmt_names(const Stmt& stmt, Names& names) {
    if (!stmt.name.empty()) {
        names.insert(stmt.name);
    }
    if (stmt.type_ref) {
        collect_type_names(*stmt.type_ref, names);
    }
    collect_expr_names(stmt.expr, names);
    collect_expr_names(stmt.value_expr, names);
    if (stmt.target_expr) {
        collect_expr_names(*stmt.target_expr, names);
    }
    if (stmt.condition_expr) {
        collect_expr_names(*stmt.condition_expr, names);
    }
    if (stmt.message_expr) {
        collect_expr_names(*stmt.message_expr, names);
    }
    if (stmt.iterable_expr) {
        collect_expr_names(*stmt.iterable_expr, names);
    }
    if (stmt.pattern_expr) {
        collect_expr_names(*stmt.pattern_expr, names);
    }
    if (stmt.guard_expr) {
        collect_expr_names(*stmt.guard_expr, names);
    }
    for (const Stmt& child : stmt.children) {
        collect_stmt_names(child, names);
    }
}

void collect_body_names(const FunctionDecl& fn, Names& names) {
    for (const Stmt& stmt : fn.statements) {
        collect_stmt_names(stmt, names);
    }
}

void collect_signature_names(const FunctionDecl& fn, Names& names) {
    for (const ParamDecl& param : fn.params) {
        collect_type_names(param.type_ref, names);
    }
    collect_type_names(function_return_type_ref(fn), names);
}

bool header_owned_body(const FunctionDecl& fn) {
    return cpp_emit_function_has_decorator(fn, "constexpr") ||
           !generic_cpp_params_for_function(fn).empty();
}

void collect_decorator_names(const std::vector<Decorator>& decorators, Names& names) {
    for (const Decorator& decorator : decorators) {
        collect_expr_names(decorator.expr, names);
    }
}

Names public_surface_names(const ModuleAst& module) {
    Names names;
    for (const TypeAliasDecl& alias : module.aliases) {
        collect_type_names(alias.type_ref, names);
    }
    for (const EnumDecl& en : module.enums) {
        collect_type_names(en.underlying_type_ref, names);
        collect_decorator_names(en.decorators, names);
        for (const EnumValueDecl& value : en.values) {
            collect_expr_names(value.value_expr, names);
            collect_decorator_names(value.decorators, names);
            for (const EnumPayloadField& field : value.payload_fields) {
                collect_type_names(field.type_ref, names);
            }
        }
        for (const FunctionDecl& method : en.methods) {
            if (method.visibility == Visibility::Private) {
                continue;
            }
            collect_signature_names(method, names);
            if (header_owned_body(method)) {
                collect_body_names(method, names);
            }
        }
    }
    for (const ClassDecl& klass : module.classes) {
        if (klass.visibility == Visibility::Private) {
            continue;
        }
        collect_decorator_names(klass.decorators, names);
        for (const BaseClassDecl& base : klass.base_class_refs) {
            collect_type_names(base.type_ref, names);
        }
        for (const TypeAliasDecl& alias : klass.type_aliases) {
            collect_type_names(alias.type_ref, names);
        }
        for (const FieldDecl& field : klass.fields) {
            collect_type_names(field.type_ref, names);
            collect_expr_names(field.value_expr, names);
        }
        for (const ConstDecl& constant : klass.constants) {
            collect_type_names(constant.type_ref, names);
            collect_expr_names(constant.value_expr, names);
        }
        for (const ConstDecl& field : klass.static_fields) {
            collect_type_names(field.type_ref, names);
            collect_expr_names(field.value_expr, names);
        }
        const bool generic_class = !generic_cpp_params_for_class(klass).empty();
        for (const FunctionDecl& method : klass.methods) {
            collect_signature_names(method, names);
            if (generic_class || header_owned_body(method)) {
                collect_body_names(method, names);
            }
        }
    }
    for (const ConstDecl& constant : module.constants) {
        collect_type_names(constant.type_ref, names);
        collect_expr_names(constant.value_expr, names);
    }
    for (const FunctionDecl& fn : module.functions) {
        if (fn.visibility == Visibility::Private || cpp_emit_function_is_test(fn)) {
            continue;
        }
        collect_signature_names(fn, names);
        if (header_owned_body(fn)) {
            collect_body_names(fn, names);
        }
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        collect_expr_names(assertion.expression_expr, names);
    }
    return names;
}

bool name_uses_prefix(const Names& names, const std::string& prefix) {
    for (const std::string& name : names) {
        if (name == prefix || name.starts_with(prefix + ".")) {
            return true;
        }
    }
    return false;
}

bool any_native_name_used(const ModuleAst& module, const Names& names) {
    auto used = [&](const auto& declarations) {
        for (const auto& declaration : declarations) {
            if (name_uses_prefix(names, declaration.name)) {
                return true;
            }
        }
        return false;
    };
    return used(module.native_types) || used(module.native_classes) ||
           used(module.native_values) || used(module.native_functions) ||
           used(module.native_macros) || used(module.native_namespaces);
}

bool import_required_by_public_surface(const ModuleAst& module, const ImportDecl& import,
                                       const Names& names) {
    if (import.kind == ImportKind::From) {
        const std::string exposed = import.alias.empty() ? import.imported_name : import.alias;
        return name_uses_prefix(names, exposed);
    }
    if (import.kind == ImportKind::Module) {
        const std::string exposed = import.alias.empty() ? import.module_path : import.alias;
        return name_uses_prefix(names, exposed);
    }
    if (!import.alias.empty()) {
        return name_uses_prefix(names, import.alias);
    }
    return any_native_name_used(module, names);
}

} // namespace

std::vector<bool> cpp_public_import_mask(const ModuleAst& module) {
    const Names names = public_surface_names(module);
    std::vector<bool> mask;
    mask.reserve(module.imports.size());
    for (const ImportDecl& import : module.imports) {
        mask.push_back(import_required_by_public_surface(module, import, names));
    }
    return mask;
}

} // namespace dudu
