#include "dudu/macro/macro_registry.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/core/source.hpp"

#include <algorithm>
#include <set>
#include <string_view>

namespace dudu::macro {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message,
                       std::string code = "dudu.macro.definition") {
    throw CompileError(location, message, std::move(code));
}

std::string expression_path(const Expr& expr, const SourceLocation& location,
                            std::string_view context) {
    const std::optional<ExprPath> path = expr_path_from_expr(expr);
    if (!path.has_value()) {
        fail(location, std::string(context) + " must be a macro name");
    }
    return render_expr_path(*path);
}

const Decorator* macro_decorator(const FunctionDecl& function) {
    for (const Decorator& decorator : function.decorators) {
        if (decorator_matches(decorator, "macro")) {
            return &decorator;
        }
    }
    return nullptr;
}

TargetKind target_kind_from_parameter(const ParamDecl& parameter) {
    const std::string name = type_ref_head_name(parameter.type_ref);
    if (name.ends_with(".ClassDecl") || name == "ClassDecl")
        return TargetKind::Class;
    if (name.ends_with(".EnumDecl") || name == "EnumDecl")
        return TargetKind::Enum;
    if (name.ends_with(".FunctionDecl") || name == "FunctionDecl")
        return TargetKind::Function;
    if (name.ends_with(".FieldDecl") || name == "FieldDecl")
        return TargetKind::Field;
    if (name.ends_with(".ConstantDecl") || name == "ConstantDecl")
        return TargetKind::Constant;
    if (name.ends_with(".Declaration") || name == "Declaration")
        return TargetKind::Any;
    fail(parameter.location, "macro input must be a dudu.ast declaration type");
}

bool is_expansion_type(const TypeRef& type) {
    const std::string name = type_ref_head_name(type);
    return name == "Expansion" || name.ends_with(".Expansion");
}

const ClassDecl* local_class(const ModuleAst& module, std::string_view name) {
    for (const ClassDecl& klass : module.classes) {
        if (klass.name == name) {
            return &klass;
        }
    }
    return nullptr;
}

std::string helper_schema_name(const Decorator& decorator) {
    if (decorator.expr.kind == ExprKind::Name) {
        return {};
    }
    if (!decorator_call_matches(decorator, "macro")) {
        fail(decorator.location, "@macro must be bare or called with attributes=Schema");
    }
    std::string schema;
    for (const Expr& argument : decorator.expr.children) {
        if (argument.kind != ExprKind::NamedArg || argument.name != "attributes" ||
            argument.children.size() != 1) {
            fail(argument.location,
                 "@macro accepts only the named argument attributes=Schema");
        }
        if (!schema.empty()) {
            fail(argument.location, "duplicate @macro attributes argument");
        }
        schema = expression_path(argument.children.front(), argument.location,
                                 "@macro attributes schema");
        if (schema.find('.') != std::string::npos) {
            fail(argument.location, "@macro attributes schema must be a local class");
        }
    }
    return schema;
}

Definition definition_from_function(const ModuleAst& module, const FunctionDecl& function,
                                    const Decorator& decorator) {
    if (function.params.size() != 1) {
        fail(function.location, "macro function must have exactly one declaration parameter");
    }
    if (function_has_receiver_type(function)) {
        fail(function.location, "macro function must be declared at module scope");
    }
    if (!function.generic_params.empty()) {
        fail(function.location, "macro function cannot declare generic parameters");
    }
    if (!function_has_return_type(function) || !is_expansion_type(function.return_type_ref)) {
        fail(function.location, "macro function must return dudu.ast.Expansion");
    }
    Definition definition;
    definition.name = function.name;
    definition.module_path = module.module_path;
    definition.identity = module.module_path + "." + function.name;
    definition.accepted_kind = target_kind_from_parameter(function.params.front());
    definition.function = &function;
    definition.location = function.location;
    const std::string schema_name = helper_schema_name(decorator);
    if (!schema_name.empty()) {
        definition.attribute_schema = local_class(module, schema_name);
        if (definition.attribute_schema == nullptr) {
            fail(decorator.location, "unknown @macro attribute schema: " + schema_name);
        }
    }
    return definition;
}

std::vector<const ModuleAst*> source_units(const ModuleAst& module) {
    if (module.module_units.empty()) {
        return {&module};
    }
    std::vector<const ModuleAst*> out;
    out.reserve(module.module_units.size());
    for (const ModuleAst& unit : module.module_units) {
        out.push_back(&unit);
    }
    return out;
}

std::string resolved_import_module(const ModuleAst& unit, const ImportDecl& import) {
    for (const ModuleDependency& dependency : unit.dependencies) {
        if (dependency.kind == import.kind &&
            dependency.import_module_path == import.module_path) {
            return dependency.resolved_module_path;
        }
    }
    return import.module_path;
}

const Definition* definition_by_identity(const Plan& plan, const std::string& identity) {
    const auto found = plan.definitions.find(identity);
    return found == plan.definitions.end() ? nullptr : &found->second;
}

const Definition* resolve_definition(const Plan& plan, const ModuleAst& unit,
                                     const std::string& reference) {
    if (const Definition* local =
            definition_by_identity(plan, unit.module_path + "." + reference)) {
        return local;
    }
    for (const ImportDecl& import : unit.imports) {
        if (import.kind == ImportKind::From) {
            const std::string exposed = import.alias.empty() ? import.imported_name : import.alias;
            if (reference == exposed) {
                return definition_by_identity(
                    plan, resolved_import_module(unit, import) + "." + import.imported_name);
            }
            continue;
        }
        if (import.kind != ImportKind::Module) {
            continue;
        }
        const std::string prefix = import.alias.empty() ? import.module_path : import.alias;
        if (!reference.starts_with(prefix + ".")) {
            continue;
        }
        const std::string member = reference.substr(prefix.size() + 1);
        return definition_by_identity(plan, resolved_import_module(unit, import) + "." + member);
    }
    return nullptr;
}

bool builtin_decorator(std::string_view name) {
    static const std::set<std::string_view> builtins = {
        "abstract",      "align",       "constexpr",  "cuda.device", "cuda.global",
        "cuda.host",     "extern_c",    "inline",     "operator",    "override",
        "packed",        "section",     "shader.compute", "test",    "test.ignore",
        "test.should_panic", "virtual", "workgroup_size"};
    return builtins.contains(name);
}

bool target_accepts(TargetKind accepted, TargetKind actual) {
    return accepted == TargetKind::Any || accepted == actual;
}

std::vector<const Definition*> derive_definitions(const Plan& plan, const ModuleAst& unit,
                                                  const Decorator& decorator) {
    if (!decorator_call_matches(decorator, "derive") || decorator.expr.children.empty()) {
        fail(decorator.location, "@derive requires one or more imported macro names");
    }
    std::vector<const Definition*> out;
    std::set<std::string> identities;
    for (const Expr& argument : decorator.expr.children) {
        const std::string reference = expression_path(argument, argument.location, "derive argument");
        const Definition* definition = resolve_definition(plan, unit, reference);
        if (definition == nullptr) {
            fail(argument.location, "unknown derive macro: " + reference,
                 "dudu.macro.unresolved");
        }
        if (!identities.insert(definition->identity).second) {
            fail(argument.location, "duplicate derive macro: " + reference);
        }
        out.push_back(definition);
    }
    return out;
}

const FieldDecl* schema_field(const ClassDecl& schema, std::string_view name) {
    for (const FieldDecl& field : schema.fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

bool literal_matches_type(const Expr& value, const TypeRef& type) {
    if (type.kind == TypeKind::Template && type.name == "Option" && !type.children.empty()) {
        return value.kind == ExprKind::NoneLiteral || literal_matches_type(value, type.children[0]);
    }
    const std::string name = type_ref_head_name(type);
    if (name == "bool")
        return value.kind == ExprKind::BoolLiteral;
    if (name == "str")
        return value.kind == ExprKind::StringLiteral;
    if (name.starts_with("i") || name.starts_with("u"))
        return value.kind == ExprKind::IntLiteral;
    if (name.starts_with("f"))
        return value.kind == ExprKind::FloatLiteral || value.kind == ExprKind::IntLiteral;
    return value.kind == ExprKind::Name || value.kind == ExprKind::Member ||
           value.kind == ExprKind::Call;
}

void validate_helper_arguments(const Definition& definition, const Decorator& decorator) {
    if (definition.attribute_schema == nullptr) {
        fail(decorator.location, "macro '" + definition.name +
                                     "' does not declare helper attributes");
    }
    if (!decorator_call_matches(decorator, definition.name) &&
        !decorator_call_matches(decorator, definition.identity)) {
        if (decorator.expr.kind == ExprKind::Name) {
            return;
        }
    }
    std::set<std::string> supplied;
    for (const Expr& argument : decorator.expr.children) {
        if (argument.kind != ExprKind::NamedArg || argument.children.size() != 1) {
            fail(argument.location, "helper attributes require named arguments");
        }
        const FieldDecl* field = schema_field(*definition.attribute_schema, argument.name);
        if (field == nullptr) {
            fail(argument.location, "unknown " + definition.name + " attribute option: " +
                                        argument.name);
        }
        if (!supplied.insert(argument.name).second) {
            fail(argument.location, "duplicate " + definition.name + " attribute option: " +
                                        argument.name);
        }
        if (!literal_matches_type(argument.children.front(), field->type_ref)) {
            fail(argument.location, "wrong value type for " + definition.name + "." + field->name);
        }
    }
    for (const FieldDecl& field : definition.attribute_schema->fields) {
        if (!expr_present(field.value_expr) && !supplied.contains(field.name)) {
            fail(decorator.location, "missing required " + definition.name +
                                         " attribute option: " + field.name);
        }
    }
}

} // namespace

std::string_view target_kind_name(TargetKind kind) {
    switch (kind) {
    case TargetKind::Any:
        return "declaration";
    case TargetKind::Class:
        return "class";
    case TargetKind::Enum:
        return "enum";
    case TargetKind::Function:
        return "function";
    case TargetKind::Field:
        return "field";
    case TargetKind::Constant:
        return "constant";
    }
    return "declaration";
}

// Invocation collection is kept separate from worker execution so CLI and LSP share it.
Plan build_plan(const ModuleAst& module) {
    Plan plan;
    const std::vector<const ModuleAst*> units = source_units(module);
    for (const ModuleAst* unit : units) {
        for (const FunctionDecl& function : unit->functions) {
            if (const Decorator* decorator = macro_decorator(function)) {
                Definition definition = definition_from_function(*unit, function, *decorator);
                if (!plan.definitions.emplace(definition.identity, definition).second) {
                    fail(function.location, "duplicate macro export: " + definition.identity);
                }
            }
        }
    }

    const auto collect_decorators = [&](const ModuleAst& unit, const std::vector<Decorator>& decorators,
                                        TargetKind kind, const std::string& name,
                                        std::vector<Invocation>& invocations) {
        std::vector<const Definition*> active;
        for (const Decorator& decorator : decorators) {
            if (decorator_matches(decorator, "derive")) {
                for (const Definition* definition : derive_definitions(plan, unit, decorator)) {
                    if (!target_accepts(definition->accepted_kind, kind)) {
                        fail(decorator.location, "macro '" + definition->name + "' accepts " +
                                                     std::string(target_kind_name(
                                                         definition->accepted_kind)) +
                                                     ", not " + std::string(target_kind_name(kind)));
                    }
                    invocations.push_back({.macro = definition,
                                           .decorator = &decorator,
                                           .target_module = unit.module_path,
                                           .target_name = name,
                                           .target_kind = kind,
                                           .derive = true,
                                           .helper_attributes = {}});
                    active.push_back(definition);
                }
                continue;
            }
            const std::string reference = decorator_name(decorator);
            if (builtin_decorator(reference) || reference == "macro") {
                continue;
            }
            const Definition* definition = resolve_definition(plan, unit, reference);
            if (definition == nullptr) {
                continue;
            }
            if (!target_accepts(definition->accepted_kind, kind)) {
                continue;
            }
            invocations.push_back({.macro = definition,
                                   .decorator = &decorator,
                                   .target_module = unit.module_path,
                                   .target_name = name,
                                   .target_kind = kind,
                                   .derive = false,
                                   .helper_attributes = {}});
            active.push_back(definition);
        }
        return active;
    };

    const auto collect_helpers = [&](const ModuleAst& unit,
                                     const std::vector<const Definition*>& active,
                                     const std::vector<Decorator>& decorators, TargetKind kind,
                                     const std::string& name, Invocation& parent) {
        for (const Decorator& decorator : decorators) {
            const std::string reference = decorator_name(decorator);
            const Definition* definition = resolve_definition(plan, unit, reference);
            if (definition == nullptr) {
                continue;
            }
            if (std::find(active.begin(), active.end(), definition) == active.end()) {
                continue;
            }
            validate_helper_arguments(*definition, decorator);
            parent.helper_attributes.push_back({.macro = definition,
                                                .decorator = &decorator,
                                                .target_name = name,
                                                .target_kind = kind});
        }
    };

    for (const ModuleAst* unit : units) {
        for (const ClassDecl& klass : unit->classes) {
            const std::size_t first = plan.invocations.size();
            const std::vector<const Definition*> active = collect_decorators(
                *unit, klass.decorators, TargetKind::Class, klass.name, plan.invocations);
            for (std::size_t index = first; index < plan.invocations.size(); ++index) {
                for (const FieldDecl& field : klass.fields) {
                    collect_helpers(*unit, active, field.decorators, TargetKind::Field, field.name,
                                    plan.invocations[index]);
                }
                for (const ConstDecl& constant : klass.constants) {
                    collect_helpers(*unit, active, constant.decorators, TargetKind::Constant,
                                    constant.name, plan.invocations[index]);
                }
                for (const FunctionDecl& method : klass.methods) {
                    collect_helpers(*unit, active, method.decorators, TargetKind::Function,
                                    method.name, plan.invocations[index]);
                }
            }
        }
        for (const EnumDecl& en : unit->enums) {
            const std::size_t first = plan.invocations.size();
            const std::vector<const Definition*> active = collect_decorators(
                *unit, en.decorators, TargetKind::Enum, en.name, plan.invocations);
            for (std::size_t index = first; index < plan.invocations.size(); ++index) {
                for (const EnumValueDecl& value : en.values) {
                    collect_helpers(*unit, active, value.decorators, TargetKind::Field, value.name,
                                    plan.invocations[index]);
                    for (const EnumPayloadField& field : value.payload_fields) {
                        collect_helpers(*unit, active, field.decorators, TargetKind::Field,
                                        field.name, plan.invocations[index]);
                    }
                }
            }
        }
        for (const FunctionDecl& function : unit->functions) {
            if (macro_decorator(function) == nullptr) {
                (void)collect_decorators(*unit, function.decorators, TargetKind::Function,
                                         function.name, plan.invocations);
            }
        }
        for (const ConstDecl& constant : unit->constants) {
            (void)collect_decorators(*unit, constant.decorators, TargetKind::Constant,
                                     constant.name, plan.invocations);
        }
    }
    return plan;
}

} // namespace dudu::macro
