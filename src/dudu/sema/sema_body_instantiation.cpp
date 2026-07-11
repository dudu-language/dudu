#include "dudu/core/ast_type.hpp"
#include "dudu/core/shape_value_expr.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_body_internal.hpp"
#include "dudu/sema/sema_body_substitution.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace dudu {
namespace {

thread_local std::set<std::string> active_instantiations;

void check_instantiated_body(const FunctionScope& caller_scope, const FunctionDecl& fn,
                             BodyTypeSubstitutions substitutions, std::string instantiation,
                             const SourceLocation& site, std::string current_class) {
    const std::string instantiation_key = fn.origin_module + "::" + fn.cpp_name +
                                          "::" + format_location(fn.location) +
                                          "::" + instantiation;
    if (active_instantiations.contains(instantiation_key)) {
        return;
    }

    std::vector<Stmt> body = substitute_body_types(fn.statements, substitutions);
    const TypeRef return_type_ref =
        function_has_return_type(fn) ? substitute_body_type_ref(fn.return_type_ref, substitutions)
                                     : void_type_ref(fn.location);

    FunctionScope scope{caller_scope.symbols};
    scope.constants = caller_scope.constants;
    scope.target_mode = caller_scope.target_mode;
    scope.current_class = std::move(current_class);
    scope.allow_super_init = fn.name == "init";
    scope.return_type_ref = return_type_ref;
    for (const ParamDecl& param : fn.params) {
        bind_local(scope, param.name, substitute_body_type_ref(param.type_ref, substitutions));
    }

    active_instantiations.insert(instantiation_key);
    try {
        check_function_body_statements(scope, body, return_type_ref);
    } catch (const CompileError& error) {
        active_instantiations.erase(instantiation_key);
        throw CompileError(error.location(),
                           std::string(error.what()) + " while instantiating " + instantiation +
                               " at " + format_location(site),
                           error.code(), error.data_name());
    }
    active_instantiations.erase(instantiation_key);
}

void merge_substitutions(BodyTypeSubstitutions& target, const BodyTypeSubstitutions& source) {
    target.scalar.insert(source.scalar.begin(), source.scalar.end());
    target.packs.insert(source.packs.begin(), source.packs.end());
}

const ModuleAst* declaration_unit(const Symbols& symbols, std::string_view module_path) {
    if (symbols.module_tree == nullptr || module_path.empty()) {
        return nullptr;
    }
    for (const ModuleAst& unit : symbols.module_tree->module_units) {
        if (unit.module_path == module_path) {
            return &unit;
        }
    }
    return nullptr;
}

const ClassDecl* declaration_class(const ModuleAst& unit, const ClassDecl& imported) {
    for (const ClassDecl& klass : unit.classes) {
        if ((!imported.cpp_name.empty() && klass.cpp_name == imported.cpp_name) ||
            (klass.origin_module == imported.origin_module &&
             imported.name.ends_with(klass.name))) {
            return &klass;
        }
    }
    return nullptr;
}

const FunctionDecl* declaration_function(const ModuleAst& unit, const FunctionDecl& imported) {
    for (const FunctionDecl& fn : unit.functions) {
        if (&fn == &imported) {
            return &fn;
        }
    }
    for (const FunctionDecl& fn : unit.functions) {
        if ((!imported.cpp_name.empty() && fn.cpp_name == imported.cpp_name) ||
            (fn.origin_module == imported.origin_module && fn.name == imported.name)) {
            return &fn;
        }
    }
    return nullptr;
}

bool scope_owns_function(const FunctionScope& scope, const FunctionDecl* fn) {
    for (const auto& [name, declarations] : scope.symbols.function_overload_decls) {
        (void)name;
        if (std::ranges::find(declarations, fn) != declarations.end()) {
            return true;
        }
    }
    return false;
}

std::string function_identity(const FunctionDecl& fn) {
    return fn.origin_module.empty() ? fn.name : fn.origin_module + "." + fn.name;
}

std::vector<const FunctionDecl*> imported_function_declarations(const FunctionScope& scope,
                                                                const std::string& callee) {
    std::vector<const FunctionDecl*> out;
    if (scope.symbols.module_tree == nullptr) {
        return out;
    }
    const std::vector<const NativeFunctionDecl*> aliases =
        native_function_decls_for_binding(scope.symbols, callee);
    if (aliases.empty()) {
        return out;
    }
    for (const NativeFunctionDecl* alias : aliases) {
        if (alias == nullptr || alias->identity.canonical_path.empty()) {
            continue;
        }
        for (const ModuleAst& unit : scope.symbols.module_tree->module_units) {
            for (const FunctionDecl& fn : unit.functions) {
                if (!fn.generic_params.empty() &&
                    function_identity(fn) == alias->identity.canonical_path) {
                    out.push_back(&fn);
                }
            }
        }
    }
    return out;
}

TypeRef instantiated_owner_type(const ClassDecl& owner, const std::vector<TypeRef>& receiver_args,
                                SourceLocation location) {
    TypeRef type = named_type_ref(owner.name, location);
    if (!receiver_args.empty()) {
        type.kind = TypeKind::Template;
        type.children = receiver_args;
    }
    return type;
}

bool contains_unresolved_generic(const TypeRef& type, const Symbols& symbols) {
    if (symbols.generic_params.contains(type_ref_head_name(type))) {
        return true;
    }
    if (type.kind == TypeKind::Value) {
        const std::set<std::string> identifiers = shape_value_expr_identifiers(type.value);
        for (const std::string& identifier : identifiers) {
            if (symbols.generic_params.contains(identifier)) {
                return true;
            }
        }
    }
    for (const TypeRef& child : type.children) {
        if (contains_unresolved_generic(child, symbols)) {
            return true;
        }
    }
    return false;
}

bool has_unresolved_generic(const std::vector<TypeRef>& args, const Symbols& symbols) {
    return std::ranges::any_of(
        args, [&](const TypeRef& arg) { return contains_unresolved_generic(arg, symbols); });
}

} // namespace

void check_instantiated_generic_function_body(const FunctionScope& caller_scope,
                                              const FunctionDecl& fn,
                                              const std::vector<TypeRef>& type_args,
                                              const std::string& label,
                                              const SourceLocation& site) {
    if (const ModuleAst* unit = declaration_unit(caller_scope.symbols, fn.origin_module)) {
        if (const FunctionDecl* declared = declaration_function(*unit, fn);
            declared != nullptr && !scope_owns_function(caller_scope, declared)) {
            Symbols declaration_symbols = collect_symbols(*unit);
            declaration_symbols.module_tree = caller_scope.symbols.module_tree;
            FunctionScope declaration_scope{declaration_symbols};
            declaration_scope.constants = caller_scope.constants;
            declaration_scope.target_mode = caller_scope.target_mode;
            check_instantiated_generic_function_body(declaration_scope, *declared, type_args, label,
                                                     site);
            return;
        }
    }
    const std::string instantiation =
        label.empty() ? body_instantiated_label(fn.name, type_args) : label;
    check_instantiated_body(caller_scope, fn, body_type_substitutions(fn.generic_params, type_args),
                            instantiation, site, {});
}

void check_instantiated_imported_generic_function_body(
    const FunctionScope& caller_scope, const std::string& callee, const std::vector<Expr>& args,
    const std::optional<std::vector<TypeRef>>& explicit_type_args, const SourceLocation& site) {
    for (const FunctionDecl* fn : imported_function_declarations(caller_scope, callee)) {
        std::optional<std::vector<TypeRef>> type_args = explicit_type_args;
        if (!type_args) {
            type_args = infer_generic_call_type_args(caller_scope, *fn, callee, args, nullptr);
        }
        if (!type_args || !generic_arity_matches(fn->generic_params, type_args->size())) {
            continue;
        }
        const FunctionSignature signature = instantiate_generic_signature(*fn, *type_args);
        if (!matching_signature_ast(caller_scope, {signature}, args)) {
            continue;
        }
        check_instantiated_generic_function_body(caller_scope, *fn, *type_args, "", site);
        return;
    }
}

void check_instantiated_generic_method_body(const FunctionScope& caller_scope,
                                            const ClassDecl& owner, const FunctionDecl& method,
                                            const TypeRef& receiver_type,
                                            const std::vector<TypeRef>& receiver_args,
                                            const std::vector<TypeRef>& method_args,
                                            const SourceLocation& site) {
    if (owner.generic_params.empty() && method.generic_params.empty()) {
        return;
    }
    if (has_unresolved_generic(receiver_args, caller_scope.symbols) ||
        has_unresolved_generic(method_args, caller_scope.symbols)) {
        return;
    }
    if (const ModuleAst* unit = declaration_unit(caller_scope.symbols, owner.origin_module)) {
        if (const ClassDecl* declared_owner = declaration_class(*unit, owner);
            declared_owner != nullptr && declared_owner != &owner) {
            const size_t method_index = static_cast<size_t>(&method - owner.methods.data());
            if (method_index < declared_owner->methods.size()) {
                Symbols declaration_symbols = collect_symbols(*unit);
                declaration_symbols.module_tree = caller_scope.symbols.module_tree;
                FunctionScope declaration_scope{declaration_symbols};
                declaration_scope.constants = caller_scope.constants;
                declaration_scope.target_mode = caller_scope.target_mode;
                const TypeRef declared_receiver =
                    instantiated_owner_type(*declared_owner, receiver_args, receiver_type.location);
                check_instantiated_generic_method_body(
                    declaration_scope, *declared_owner, declared_owner->methods[method_index],
                    declared_receiver, receiver_args, method_args, site);
                return;
            }
        }
    }
    BodyTypeSubstitutions substitutions =
        body_type_substitutions(owner.generic_params, receiver_args);
    merge_substitutions(substitutions, body_type_substitutions(method.generic_params, method_args));
    substitutions.scalar.insert_or_assign("Self", receiver_type);

    std::string instantiation = type_ref_text(receiver_type) + "." + method.name;
    if (!method.generic_params.empty()) {
        instantiation += body_instantiated_label("", method_args);
    }
    check_instantiated_body(caller_scope, method, std::move(substitutions),
                            std::move(instantiation), site, owner.name);
}

} // namespace dudu
