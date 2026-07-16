#include "dudu/codegen/cpp_module_dependencies.hpp"

#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <set>
#include <string>

namespace dudu {
namespace {

using Names = std::set<std::string>;

struct PublicSurface {
    Names names;
    bool opaque_native_use = false;
};

void collect_type_names(const TypeRef& type, Names& names) {
    if (!type.name.empty()) {
        names.insert(type.name.str());
    }
    for (const TypeRef& child : type.children) {
        collect_type_names(child, names);
    }
}

bool collect_expr_names(const Expr& expr, Names& names) {
    bool opaque_native_use = expr.kind == ExprKind::CppEscape;
    if (!expr.name.empty()) {
        names.insert(expr.name.str());
    }
    if (expr.type_ref) {
        collect_type_names(*expr.type_ref, names);
    }
    if (expr.callee) {
        for (const Expr& child : *expr.callee) {
            opaque_native_use |= collect_expr_names(child, names);
        }
    }
    if (expr.template_args) {
        for (const Expr& child : *expr.template_args) {
            opaque_native_use |= collect_expr_names(child, names);
        }
    }
    if (expr.template_type_args) {
        for (const TypeRef& type : *expr.template_type_args) {
            collect_type_names(type, names);
        }
    }
    for (const Expr& child : expr.children) {
        opaque_native_use |= collect_expr_names(child, names);
    }
    return opaque_native_use;
}

bool collect_stmt_names(const Stmt& stmt, Names& names) {
    bool opaque_native_use = stmt.kind == StmtKind::CppEscape;
    if (!stmt.name.empty()) {
        names.insert(stmt.name);
    }
    if (stmt.type_ref) {
        collect_type_names(*stmt.type_ref, names);
    }
    opaque_native_use |= collect_expr_names(stmt.expr, names);
    opaque_native_use |= collect_expr_names(stmt.value_expr, names);
    if (stmt.target_expr) {
        opaque_native_use |= collect_expr_names(*stmt.target_expr, names);
    }
    if (stmt.condition_expr) {
        opaque_native_use |= collect_expr_names(*stmt.condition_expr, names);
    }
    if (stmt.message_expr) {
        opaque_native_use |= collect_expr_names(*stmt.message_expr, names);
    }
    if (stmt.iterable_expr) {
        opaque_native_use |= collect_expr_names(*stmt.iterable_expr, names);
    }
    if (stmt.pattern_expr) {
        opaque_native_use |= collect_expr_names(*stmt.pattern_expr, names);
    }
    if (stmt.guard_expr) {
        opaque_native_use |= collect_expr_names(*stmt.guard_expr, names);
    }
    for (const Stmt& child : stmt.children) {
        opaque_native_use |= collect_stmt_names(child, names);
    }
    return opaque_native_use;
}

bool collect_body_names(const FunctionDecl& fn, Names& names) {
    bool opaque_native_use = false;
    for (const Stmt& stmt : fn.statements) {
        opaque_native_use |= collect_stmt_names(stmt, names);
    }
    return opaque_native_use;
}

void collect_signature_names(const FunctionDecl& fn, Names& names) {
    for (const ParamDecl& param : fn.params) {
        collect_type_names(param.type_ref, names);
    }
    collect_type_names(function_return_type_ref(fn), names);
}

bool header_owned_body(const FunctionDecl& fn) {
    return has_decorator(fn, "constexpr") || !generic_cpp_params_for_function(fn).empty();
}

bool collect_decorator_names(const std::vector<Decorator>& decorators, Names& names) {
    bool opaque_native_use = false;
    for (const Decorator& decorator : decorators) {
        opaque_native_use |= collect_expr_names(decorator.expr, names);
    }
    return opaque_native_use;
}

PublicSurface public_surface(const ModuleAst& module) {
    PublicSurface surface;
    Names& names = surface.names;
    for (const TypeAliasDecl& alias : module.aliases) {
        collect_type_names(alias.type_ref, names);
    }
    for (const EnumDecl& en : module.enums) {
        collect_type_names(en.underlying_type_ref, names);
        surface.opaque_native_use |= collect_decorator_names(en.decorators, names);
        for (const EnumValueDecl& value : en.values) {
            surface.opaque_native_use |= collect_expr_names(value.value_expr, names);
            surface.opaque_native_use |= collect_decorator_names(value.decorators, names);
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
                surface.opaque_native_use |= collect_body_names(method, names);
            }
        }
    }
    for (const ClassDecl& klass : module.classes) {
        if (klass.visibility == Visibility::Private) {
            continue;
        }
        surface.opaque_native_use |= collect_decorator_names(klass.decorators, names);
        for (const BaseClassDecl& base : klass.base_class_refs) {
            collect_type_names(base.type_ref, names);
        }
        for (const TypeAliasDecl& alias : klass.type_aliases) {
            collect_type_names(alias.type_ref, names);
        }
        for (const FieldDecl& field : klass.fields) {
            collect_type_names(field.type_ref, names);
            surface.opaque_native_use |= collect_expr_names(field.value_expr, names);
        }
        for (const ConstDecl& constant : klass.constants) {
            collect_type_names(constant.type_ref, names);
            surface.opaque_native_use |= collect_expr_names(constant.value_expr, names);
        }
        for (const ConstDecl& field : klass.static_fields) {
            collect_type_names(field.type_ref, names);
            surface.opaque_native_use |= collect_expr_names(field.value_expr, names);
        }
        const bool generic_class = !generic_cpp_params_for_class(klass).empty();
        for (const FunctionDecl& method : klass.methods) {
            collect_signature_names(method, names);
            if (generic_class || header_owned_body(method)) {
                surface.opaque_native_use |= collect_body_names(method, names);
            }
        }
    }
    for (const ConstDecl& constant : module.constants) {
        collect_type_names(constant.type_ref, names);
        surface.opaque_native_use |= collect_expr_names(constant.value_expr, names);
    }
    for (const FunctionDecl& fn : module.functions) {
        if (fn.visibility == Visibility::Private || is_test_function(fn)) {
            continue;
        }
        collect_signature_names(fn, names);
        if (header_owned_body(fn)) {
            surface.opaque_native_use |= collect_body_names(fn, names);
        }
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        surface.opaque_native_use |= collect_expr_names(assertion.expression_expr, names);
    }
    return surface;
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
    return used(module.native_types) || used(module.native_classes) || used(module.native_values) ||
           used(module.native_functions) || used(module.native_macros) ||
           used(module.native_namespaces);
}

bool import_required_by_public_surface(const ModuleAst& module, const ImportDecl& import,
                                       const PublicSurface& surface) {
    const Names& names = surface.names;
    if (import.kind == ImportKind::From) {
        const std::string exposed = import.alias.empty() ? import.imported_name : import.alias;
        return name_uses_prefix(names, exposed);
    }
    if (import.kind == ImportKind::Module) {
        const std::string exposed = import.alias.empty() ? import.module_path : import.alias;
        return name_uses_prefix(names, exposed);
    }
    if (!import.alias.empty()) {
        return surface.opaque_native_use || name_uses_prefix(names, import.alias);
    }
    return surface.opaque_native_use || any_native_name_used(module, names);
}

} // namespace

std::vector<bool> cpp_public_import_mask(const ModuleAst& module) {
    const PublicSurface surface = public_surface(module);
    std::vector<bool> mask;
    mask.reserve(module.imports.size());
    for (const ImportDecl& import : module.imports) {
        mask.push_back(import_required_by_public_surface(module, import, surface));
    }
    return mask;
}

} // namespace dudu
