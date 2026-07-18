#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/macro/macro_diagnostic_bridge.hpp"
#include "dudu/macro/macro_expansion_internal.hpp"
#include "dudu/project/module_import_aliases.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace dudu::macro {
namespace {

namespace p = protocol;

using NameSet = std::unordered_set<std::string>;

struct UnitMergeState {
    ModuleAst* unit = nullptr;
    std::unordered_map<std::string, std::size_t> classes;
    std::unordered_map<std::string, std::size_t> enums;
    std::unordered_map<std::string, NameSet> class_members;
    std::unordered_map<std::string, NameSet> enum_members;
    NameSet module_names;
};

[[noreturn]] void fail(const CollectedExpansion& source, const std::string& message,
                       std::string code = "dudu.macro.output") {
    throw CompileError(source.invocation.start, message, std::move(code));
}

std::vector<ModuleAst*> units(ModuleAst& module) {
    if (module.module_units.empty())
        return {&module};
    std::vector<ModuleAst*> out;
    out.reserve(module.module_units.size());
    for (ModuleAst& unit : module.module_units)
        out.push_back(&unit);
    return out;
}

ModuleAst* unit_named(ModuleAst& module, std::string_view module_path) {
    for (ModuleAst* unit : units(module)) {
        if (unit->module_path == module_path)
            return unit;
    }
    return nullptr;
}

const Definition* macro_definition(const Plan& plan, std::string_view identity) {
    const auto found = plan.definitions.find(std::string(identity));
    return found == plan.definitions.end() ? nullptr : &found->second;
}

const ModuleDependency* declared_macro_dependency(const ModuleAst& definition_unit,
                                                  std::string_view module_path) {
    for (const ModuleDependency& dependency : definition_unit.dependencies) {
        if ((dependency.kind == ImportKind::Module || dependency.kind == ImportKind::From) &&
            (dependency.import_module_path == module_path ||
             dependency.resolved_module_path == module_path)) {
            return &dependency;
        }
    }
    return nullptr;
}

bool import_alias_exists(const ModuleAst& unit, std::string_view alias) {
    return std::any_of(unit.imports.begin(), unit.imports.end(), [&](const ImportDecl& import) {
        return bound_import_name(import) == alias;
    });
}

void merge_generated_imports(ModuleAst& module, UnitMergeState& state, const Plan& plan,
                             const CollectedExpansion& source) {
    if (source.expansion.imports.empty())
        return;
    const Definition* definition = macro_definition(plan, source.macro_identity);
    if (definition == nullptr)
        fail(source, "macro definition disappeared before generated imports were merged");
    ModuleAst* definition_unit = unit_named(module, definition->module_path);
    if (definition_unit == nullptr)
        fail(source, "macro definition module disappeared before generated imports were merged");

    std::set<std::pair<std::string, std::string>> seen;
    for (const p::GeneratedImport& required : source.expansion.imports) {
        if (!seen.emplace(required.module_path, required.alias).second)
            continue;
        const ModuleDependency* declared =
            declared_macro_dependency(*definition_unit, required.module_path);
        if (declared == nullptr) {
            fail(source,
                 "macro generated import is not a declared dependency of its definition "
                 "module: " +
                     required.module_path,
                 "dudu.macro.import_dependency");
        }
        ModuleAst* dependency = unit_named(module, declared->resolved_module_path);
        if (dependency == nullptr)
            fail(source,
                 "macro generated import module is not in the loaded project graph: " +
                     declared->resolved_module_path,
                 "dudu.macro.import_dependency");
        if (dependency->compilation_domain == CompilationDomain::MacroHost) {
            fail(source,
                 "macro generated import cannot expose a macro-host module to target "
                 "code: " +
                     declared->resolved_module_path,
                 "dudu.macro.import_domain");
        }
        if (state.module_names.contains(required.alias) ||
            import_alias_exists(*state.unit, required.alias)) {
            fail(source, "hygienic generated import alias unexpectedly collides: " + required.alias,
                 "dudu.macro.import_collision");
        }

        ImportDecl import;
        import.kind = ImportKind::Module;
        import.module_path = required.module_path;
        import.alias = required.alias;
        import.location = source.invocation.start;
        import.range = source.invocation;
        import.module_range = source.invocation;
        import.alias_range = source.invocation;
        state.unit->imports.push_back(import);
        state.unit->dependencies.push_back({.kind = ImportKind::Module,
                                            .import_module_path = required.module_path,
                                            .resolved_module_path = declared->resolved_module_path,
                                            .source_path = declared->source_path,
                                            .location = source.invocation.start});
        add_qualified_module_symbols(*state.unit, *dependency, import);
    }
}

ClassDecl* target_class(UnitMergeState& state, std::string_view name) {
    const auto found = state.classes.find(std::string(name));
    return found == state.classes.end() ? nullptr : &state.unit->classes[found->second];
}

EnumDecl* target_enum(UnitMergeState& state, std::string_view name) {
    const auto found = state.enums.find(std::string(name));
    return found == state.enums.end() ? nullptr : &state.unit->enums[found->second];
}

bool macro_decorator(const Decorator& decorator, const Plan& plan) {
    if (decorator_matches(decorator, "derive"))
        return true;
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

NameSet class_member_names(const ClassDecl& klass) {
    NameSet names;
    for (const FieldDecl& field : klass.fields)
        names.insert(field.name);
    for (const ConstDecl& constant : klass.constants)
        names.insert(constant.name);
    for (const ConstDecl& field : klass.static_fields)
        names.insert(field.name);
    for (const FunctionDecl& method : klass.methods)
        names.insert(method.name);
    return names;
}

NameSet enum_member_names(const EnumDecl& en) {
    NameSet names;
    for (const EnumValueDecl& value : en.values)
        names.insert(value.name);
    for (const FunctionDecl& method : en.methods)
        names.insert(method.name);
    return names;
}

void add_origin(ModuleAst& unit, const CollectedExpansion& source, GeneratedDeclarationKind kind,
                std::string owner, std::string name);

void merge_enum_member(ModuleAst& unit, EnumDecl& en, const p::Declaration& declaration,
                       const Plan& plan, const CollectedExpansion& source, NameSet& names) {
    if (declaration.kind != p::DeclarationKind::Function || !declaration.function_decl) {
        fail(source, "enum macros may only generate methods");
    }
    FunctionDecl method = from_protocol(*declaration.function_decl, unit.module_path, en.name,
                                        source.invocation.start);
    reject_recursive_attributes(method, plan, source);
    if (method.name.empty() || !names.insert(method.name).second) {
        fail(source, "generated enum member conflicts with existing name: " + method.name);
    }
    add_origin(unit, source, GeneratedDeclarationKind::Function, en.name, method.name);
    en.methods.push_back(std::move(method));
}

void add_origin(ModuleAst& unit, const CollectedExpansion& source, GeneratedDeclarationKind kind,
                std::string owner, std::string name) {
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
                        const Plan& plan, const CollectedExpansion& source, NameSet& names) {
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

NameSet module_names(const ModuleAst& unit) {
    NameSet names;
    for (const TypeAliasDecl& alias : unit.aliases)
        names.insert(alias.name);
    for (const EnumDecl& en : unit.enums)
        names.insert(en.name);
    for (const ClassDecl& klass : unit.classes)
        names.insert(klass.name);
    for (const FunctionDecl& function : unit.functions)
        names.insert(function.name);
    for (const ConstDecl& constant : unit.constants)
        names.insert(constant.name);
    return names;
}

void merge_sibling(UnitMergeState& state, const p::Declaration& declaration, const Plan& plan,
                   const CollectedExpansion& source) {
    ModuleAst& unit = *state.unit;
    NameSet& names = state.module_names;
    const SourceLocation fallback = source.invocation.start;
    if (declaration.kind == p::DeclarationKind::Class && declaration.class_decl) {
        ClassDecl klass = from_protocol(*declaration.class_decl, unit.module_path, fallback);
        reject_recursive_attributes(klass, plan, source);
        if (klass.name.empty() || !names.insert(klass.name).second)
            fail(source, "generated sibling conflicts with existing name: " + klass.name);
        add_origin(unit, source, GeneratedDeclarationKind::Class, {}, klass.name);
        state.classes.emplace(klass.name, unit.classes.size());
        unit.classes.push_back(std::move(klass));
        return;
    }
    if (declaration.kind == p::DeclarationKind::Enum && declaration.enum_decl) {
        EnumDecl en = from_protocol(*declaration.enum_decl, unit.module_path, fallback);
        reject_recursive_attributes(en, plan, source);
        if (en.name.empty() || !names.insert(en.name).second)
            fail(source, "generated sibling conflicts with existing name: " + en.name);
        add_origin(unit, source, GeneratedDeclarationKind::Enum, {}, en.name);
        state.enums.emplace(en.name, unit.enums.size());
        unit.enums.push_back(std::move(en));
        return;
    }
    if (declaration.kind == p::DeclarationKind::Function && declaration.function_decl) {
        FunctionDecl function =
            from_protocol(*declaration.function_decl, unit.module_path, {}, fallback);
        reject_recursive_attributes(function, plan, source);
        if (function.name.empty() || !names.insert(function.name).second)
            fail(source, "generated sibling conflicts with existing name: " + function.name);
        add_origin(unit, source, GeneratedDeclarationKind::Function, {}, function.name);
        unit.functions.push_back(std::move(function));
        return;
    }
    if (declaration.kind == p::DeclarationKind::Constant && declaration.constant_decl) {
        ConstDecl constant =
            from_protocol(*declaration.constant_decl, unit.module_path, {}, fallback);
        reject_recursive_attributes(constant, plan, source);
        if (constant.name.empty() || !names.insert(constant.name).second)
            fail(source, "generated sibling conflicts with existing name: " + constant.name);
        add_origin(unit, source, GeneratedDeclarationKind::Constant, {}, constant.name);
        unit.constants.push_back(std::move(constant));
        return;
    }
    fail(source, "macro generated an invalid sibling declaration");
}

void merge_implementation(UnitMergeState& state, const p::Declaration& declaration,
                          const Plan& plan, const CollectedExpansion& source) {
    ModuleAst& unit = *state.unit;
    if (declaration.kind != p::DeclarationKind::Implementation ||
        !declaration.implementation_decl) {
        fail(source, "macro implementation output must contain an implementation declaration");
    }
    const p::ImplementationDecl& implementation = *declaration.implementation_decl;
    const std::string target = implementation.target.name;
    ClassDecl* klass = target_class(state, target);
    if (klass == nullptr)
        fail(source, "generated implementation target is not a local class: " + target);
    const std::string contract = implementation.contract.name;
    const bool has_contract = std::any_of(
        klass->base_class_refs.begin(), klass->base_class_refs.end(),
        [&](const BaseClassDecl& base) { return type_ref_head_name(base.type_ref) == contract; });
    if (!contract.empty() && !has_contract)
        fail(source, "generated implementation target does not declare contract " + contract);
    NameSet& names =
        state.class_members.try_emplace(target, class_member_names(*klass)).first->second;
    for (const p::FunctionDecl& generated : implementation.methods) {
        p::Declaration method{.kind = p::DeclarationKind::Function, .function_decl = generated};
        merge_class_member(unit, *klass, method, plan, source, names);
    }
    add_origin(unit, source, GeneratedDeclarationKind::Implementation, target, contract);
}

std::unordered_map<std::string, UnitMergeState> index_units(ModuleAst& module) {
    std::unordered_map<std::string, UnitMergeState> indexed;
    for (ModuleAst* unit : units(module)) {
        UnitMergeState state;
        state.unit = unit;
        state.module_names = module_names(*unit);
        for (std::size_t i = 0; i < unit->classes.size(); ++i)
            state.classes.emplace(unit->classes[i].name, i);
        for (std::size_t i = 0; i < unit->enums.size(); ++i)
            state.enums.emplace(unit->enums[i].name, i);
        indexed.emplace(unit->module_path, std::move(state));
    }
    return indexed;
}

} // namespace

void merge_expansions(ModuleAst& module, const Plan& plan,
                      const std::vector<CollectedExpansion>& expansions) {
    auto indexed_units = index_units(module);
    for (const CollectedExpansion& source : expansions) {
        const auto found_unit = indexed_units.find(source.target_module);
        if (found_unit == indexed_units.end())
            fail(source, "macro target module disappeared before expansion merge");
        UnitMergeState& state = found_unit->second;
        ModuleAst& unit = *state.unit;
        for (const p::Diagnostic& diagnostic : source.expansion.diagnostics) {
            if (diagnostic.severity == p::DiagnosticSeverity::Error) {
                throw compile_error_from_macro_diagnostic(diagnostic,
                                                          to_protocol(source.invocation));
            }
        }
        merge_generated_imports(module, state, plan, source);
        if (!source.expansion.members.empty()) {
            if (source.target_kind == TargetKind::Class) {
                ClassDecl* klass = target_class(state, source.target_name);
                if (klass == nullptr)
                    fail(source, "macro target class disappeared before merge");
                NameSet& names =
                    state.class_members.try_emplace(source.target_name, class_member_names(*klass))
                        .first->second;
                for (const p::GeneratedDeclaration& generated : source.expansion.members)
                    merge_class_member(unit, *klass, generated.declaration, plan, source, names);
            } else if (source.target_kind == TargetKind::Enum) {
                EnumDecl* en = target_enum(state, source.target_name);
                if (en == nullptr)
                    fail(source, "macro target enum disappeared before merge");
                NameSet& names =
                    state.enum_members.try_emplace(source.target_name, enum_member_names(*en))
                        .first->second;
                for (const p::GeneratedDeclaration& generated : source.expansion.members)
                    merge_enum_member(unit, *en, generated.declaration, plan, source, names);
            } else {
                fail(source, "only class and enum macros may generate members");
            }
        }
        for (const p::GeneratedDeclaration& generated : source.expansion.siblings)
            merge_sibling(state, generated.declaration, plan, source);
        for (const p::GeneratedDeclaration& generated : source.expansion.implementations)
            merge_implementation(state, generated.declaration, plan, source);
    }
}

} // namespace dudu::macro
