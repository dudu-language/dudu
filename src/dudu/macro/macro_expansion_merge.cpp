#include "dudu/macro/macro_expansion_internal.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"

#include <algorithm>
#include <set>

namespace dudu::macro {
namespace {

namespace p = protocol;

[[noreturn]] void fail(const CollectedExpansion& source, const std::string& message,
                       std::string code = "dudu.macro.output") {
    throw CompileError(source.invocation.start, message, std::move(code));
}

std::vector<ModuleAst*> units(ModuleAst& module) {
    if (module.module_units.empty()) return {&module};
    std::vector<ModuleAst*> out;
    out.reserve(module.module_units.size());
    for (ModuleAst& unit : module.module_units) out.push_back(&unit);
    return out;
}

ModuleAst& target_unit(ModuleAst& module, const CollectedExpansion& source) {
    for (ModuleAst* unit : units(module)) {
        if (unit->module_path == source.target_module) return *unit;
    }
    fail(source, "macro target module disappeared before expansion merge");
}

ClassDecl* target_class(ModuleAst& unit, std::string_view name) {
    for (ClassDecl& klass : unit.classes) {
        if (klass.name == name) return &klass;
    }
    return nullptr;
}

EnumDecl* target_enum(ModuleAst& unit, std::string_view name) {
    for (EnumDecl& en : unit.enums) {
        if (en.name == name) return &en;
    }
    return nullptr;
}

bool macro_decorator(const Decorator& decorator, const Plan& plan) {
    if (decorator_matches(decorator, "derive")) return true;
    const std::string name = decorator_name(decorator);
    return std::any_of(plan.definitions.begin(), plan.definitions.end(), [&](const auto& item) {
        return name == item.second.name || name == item.second.identity;
    });
}

template <typename Decl>
void reject_recursive_attributes(const Decl& declaration, const Plan& plan,
                                 const CollectedExpansion& source) {
    for (const Decorator& decorator : declaration.decorators) {
        if (macro_decorator(decorator, plan)) {
            fail(source, "macro output cannot introduce another macro invocation");
        }
    }
}

std::set<std::string> class_member_names(const ClassDecl& klass) {
    std::set<std::string> names;
    for (const FieldDecl& field : klass.fields) names.insert(field.name);
    for (const ConstDecl& constant : klass.constants) names.insert(constant.name);
    for (const ConstDecl& field : klass.static_fields) names.insert(field.name);
    for (const FunctionDecl& method : klass.methods) names.insert(method.name);
    return names;
}

void add_origin(ModuleAst& unit, const CollectedExpansion& source,
                GeneratedDeclarationKind kind, std::string owner, std::string name) {
    unit.generated_origins.push_back({.kind = kind,
                                      .module_path = unit.module_path,
                                      .owner = std::move(owner),
                                      .name = std::move(name),
                                      .macro_name = source.macro_name,
                                      .macro_identity = source.macro_identity,
                                      .invocation = source.invocation,
                                      .definition = source.definition,
                                      .source_declaration = source.source_declaration});
}

void merge_class_member(ModuleAst& unit, ClassDecl& klass, const p::Declaration& declaration,
                        const Plan& plan, const CollectedExpansion& source,
                        std::set<std::string>& names) {
    const SourceLocation fallback = source.invocation.start;
    if (declaration.kind == p::DeclarationKind::Field && declaration.field_decl) {
        FieldDecl field = from_protocol(*declaration.field_decl, fallback);
        reject_recursive_attributes(field, plan, source);
        if (field.name.empty() || !names.insert(field.name).second)
            fail(source, "generated class member conflicts with existing name: " + field.name);
        add_origin(unit, source, GeneratedDeclarationKind::Field, klass.name, field.name);
        klass.fields.push_back(std::move(field));
        return;
    }
    if (declaration.kind == p::DeclarationKind::Function && declaration.function_decl) {
        FunctionDecl method =
            from_protocol(*declaration.function_decl, unit.module_path, klass.name, fallback);
        reject_recursive_attributes(method, plan, source);
        if (method.name.empty() || !names.insert(method.name).second)
            fail(source, "generated class member conflicts with existing name: " + method.name);
        add_origin(unit, source, GeneratedDeclarationKind::Function, klass.name, method.name);
        klass.methods.push_back(std::move(method));
        return;
    }
    if (declaration.kind == p::DeclarationKind::Constant && declaration.constant_decl) {
        ConstDecl constant =
            from_protocol(*declaration.constant_decl, unit.module_path, klass.name, fallback);
        reject_recursive_attributes(constant, plan, source);
        if (constant.name.empty() || !names.insert(constant.name).second)
            fail(source, "generated class member conflicts with existing name: " + constant.name);
        add_origin(unit, source, GeneratedDeclarationKind::Constant, klass.name, constant.name);
        klass.constants.push_back(std::move(constant));
        return;
    }
    fail(source, "macro generated an invalid class member declaration");
}

std::set<std::string> module_names(const ModuleAst& unit) {
    std::set<std::string> names;
    for (const TypeAliasDecl& alias : unit.aliases) names.insert(alias.name);
    for (const EnumDecl& en : unit.enums) names.insert(en.name);
    for (const ClassDecl& klass : unit.classes) names.insert(klass.name);
    for (const FunctionDecl& function : unit.functions) names.insert(function.name);
    for (const ConstDecl& constant : unit.constants) names.insert(constant.name);
    return names;
}

void merge_sibling(ModuleAst& unit, const p::Declaration& declaration, const Plan& plan,
                   const CollectedExpansion& source, std::set<std::string>& names) {
    const SourceLocation fallback = source.invocation.start;
    if (declaration.kind == p::DeclarationKind::Class && declaration.class_decl) {
        ClassDecl klass = from_protocol(*declaration.class_decl, unit.module_path, fallback);
        reject_recursive_attributes(klass, plan, source);
        if (klass.name.empty() || !names.insert(klass.name).second)
            fail(source, "generated sibling conflicts with existing name: " + klass.name);
        add_origin(unit, source, GeneratedDeclarationKind::Class, {}, klass.name);
        unit.classes.push_back(std::move(klass));
        return;
    }
    if (declaration.kind == p::DeclarationKind::Enum && declaration.enum_decl) {
        EnumDecl en = from_protocol(*declaration.enum_decl, unit.module_path, fallback);
        reject_recursive_attributes(en, plan, source);
        if (en.name.empty() || !names.insert(en.name).second)
            fail(source, "generated sibling conflicts with existing name: " + en.name);
        add_origin(unit, source, GeneratedDeclarationKind::Enum, {}, en.name);
        unit.enums.push_back(std::move(en));
        return;
    }
    if (declaration.kind == p::DeclarationKind::Function && declaration.function_decl) {
        FunctionDecl function = from_protocol(*declaration.function_decl, unit.module_path, {}, fallback);
        reject_recursive_attributes(function, plan, source);
        if (function.name.empty() || !names.insert(function.name).second)
            fail(source, "generated sibling conflicts with existing name: " + function.name);
        add_origin(unit, source, GeneratedDeclarationKind::Function, {}, function.name);
        unit.functions.push_back(std::move(function));
        return;
    }
    if (declaration.kind == p::DeclarationKind::Constant && declaration.constant_decl) {
        ConstDecl constant = from_protocol(*declaration.constant_decl, unit.module_path, {}, fallback);
        reject_recursive_attributes(constant, plan, source);
        if (constant.name.empty() || !names.insert(constant.name).second)
            fail(source, "generated sibling conflicts with existing name: " + constant.name);
        add_origin(unit, source, GeneratedDeclarationKind::Constant, {}, constant.name);
        unit.constants.push_back(std::move(constant));
        return;
    }
    fail(source, "macro generated an invalid sibling declaration");
}

void merge_implementation(ModuleAst& unit, const p::Declaration& declaration, const Plan& plan,
                          const CollectedExpansion& source) {
    if (declaration.kind != p::DeclarationKind::Implementation ||
        !declaration.implementation_decl) {
        fail(source, "macro implementation output must contain an implementation declaration");
    }
    const p::ImplementationDecl& implementation = *declaration.implementation_decl;
    const std::string target = implementation.target.name;
    ClassDecl* klass = target_class(unit, target);
    if (klass == nullptr) fail(source, "generated implementation target is not a local class: " + target);
    const std::string contract = implementation.contract.name;
    const bool has_contract = std::any_of(
        klass->base_class_refs.begin(), klass->base_class_refs.end(), [&](const BaseClassDecl& base) {
            return type_ref_head_name(base.type_ref) == contract;
        });
    if (!contract.empty() && !has_contract)
        fail(source, "generated implementation target does not declare contract " + contract);
    std::set<std::string> names = class_member_names(*klass);
    for (const p::FunctionDecl& generated : implementation.methods) {
        p::Declaration method{.kind = p::DeclarationKind::Function, .function_decl = generated};
        merge_class_member(unit, *klass, method, plan, source, names);
    }
    add_origin(unit, source, GeneratedDeclarationKind::Implementation, target, contract);
}

} // namespace

void merge_expansions(ModuleAst& module, const Plan& plan,
                      const std::vector<CollectedExpansion>& expansions) {
    for (const CollectedExpansion& source : expansions) {
        ModuleAst& unit = target_unit(module, source);
        for (const p::Diagnostic& diagnostic : source.expansion.diagnostics) {
            if (diagnostic.severity == p::DiagnosticSeverity::Error) {
                const SourceRange range = from_protocol(diagnostic.range, source.invocation.start);
                throw CompileError(range.start, diagnostic.message,
                                   diagnostic.code.empty() ? "dudu.macro" : diagnostic.code);
            }
        }
        std::set<std::string> siblings = module_names(unit);
        if (!source.expansion.members.empty()) {
            if (source.target_kind == TargetKind::Class) {
                ClassDecl* klass = target_class(unit, source.target_name);
                if (klass == nullptr) fail(source, "macro target class disappeared before merge");
                std::set<std::string> names = class_member_names(*klass);
                for (const p::GeneratedDeclaration& generated : source.expansion.members)
                    merge_class_member(unit, *klass, generated.declaration, plan, source, names);
            } else if (source.target_kind == TargetKind::Enum) {
                if (target_enum(unit, source.target_name) == nullptr)
                    fail(source, "macro target enum disappeared before merge");
                fail(source, "enum member generation is not representable in the compiler AST");
            } else {
                fail(source, "only class and enum macros may generate members");
            }
        }
        for (const p::GeneratedDeclaration& generated : source.expansion.siblings)
            merge_sibling(unit, generated.declaration, plan, source, siblings);
        for (const p::GeneratedDeclaration& generated : source.expansion.implementations)
            merge_implementation(unit, generated.declaration, plan, source);
    }
}

} // namespace dudu::macro
