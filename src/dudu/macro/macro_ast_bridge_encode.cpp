#include "dudu/macro/macro_ast_bridge.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <stdexcept>

namespace dudu::macro {
namespace {

namespace p = protocol;

p::SymbolIdentity identity(const std::string& module, const std::string& path,
                           const std::string& native_identity = {}) {
    return {.package = module.substr(0, module.find('.')),
            .module = module,
            .path = path,
            .native_identity = native_identity};
}

std::vector<p::Attribute> attributes(const std::vector<Decorator>& decorators) {
    std::vector<p::Attribute> out;
    out.reserve(decorators.size());
    for (const Decorator& decorator : decorators) out.push_back(to_protocol(decorator));
    return out;
}

std::vector<p::GenericParameter> generic_parameters(const std::vector<std::string>& params,
                                                    SourceLocation location) {
    std::vector<p::GenericParameter> out;
    out.reserve(params.size());
    for (const std::string& param : params) {
        out.push_back({.name = generic_param_base_name(param),
                       .type = {},
                       .default_type = {},
                       .default_value = {},
                       .variadic = generic_param_is_pack(param),
                       .range = to_protocol(location)});
    }
    return out;
}

p::Visibility visibility(Visibility value) {
    return value == Visibility::Private ? p::Visibility::Private : p::Visibility::Default;
}

p::Parameter parameter(const ParamDecl& value) {
    return {.name = value.name,
            .type = to_protocol(value.type_ref),
            .default_value = {},
            .variadic = value.variadic,
            .range = to_protocol(value.location)};
}

p::EnumVariant variant(const EnumValueDecl& value, const std::string& module,
                       const std::string& owner) {
    p::EnumVariant out{.name = value.name,
                       .fields = {},
                       .value = {},
                       .attributes = attributes(value.decorators),
                       .range = to_protocol(value.location)};
    for (const EnumPayloadField& field : value.payload_fields) {
        FieldDecl internal{.name = field.name,
                           .type_ref = field.type_ref,
                           .value_expr = {},
                           .decorators = field.decorators,
                           .location = field.location,
                           .doc_comment = {}};
        out.fields.push_back(to_protocol(internal, module, owner + "." + value.name));
    }
    if (expr_present(value.value_expr)) out.value = to_protocol(value.value_expr);
    return out;
}

const ModuleAst& unit_named(const ModuleAst& module, const std::string& path) {
    if (module.module_units.empty() && module.module_path == path) return module;
    for (const ModuleAst& unit : module.module_units) {
        if (unit.module_path == path) return unit;
    }
    throw std::runtime_error("macro invocation target module is missing: " + path);
}

template <typename T>
const T& named(const std::vector<T>& declarations, const Invocation& invocation,
               std::string_view kind) {
    for (const T& declaration : declarations) {
        if (declaration.name == invocation.target_name) return declaration;
    }
    throw std::runtime_error("macro invocation target " + std::string(kind) + " is missing: " +
                             invocation.target_module + "." + invocation.target_name);
}

} // namespace

p::FieldDecl to_protocol(const FieldDecl& field, const std::string& module_path,
                         const std::string& owner) {
    const std::string path = owner.empty() ? field.name : owner + "." + field.name;
    p::FieldDecl out{.name = field.name,
                     .type = to_protocol(field.type_ref),
                     .value = {},
                     .attributes = attributes(field.decorators),
                     .documentation = field.doc_comment,
                     .visibility = p::Visibility::Default,
                     .range = to_protocol(field.location),
                     .identity = identity(module_path, path)};
    if (expr_present(field.value_expr)) out.value = to_protocol(field.value_expr);
    return out;
}

p::FunctionDecl to_protocol(const FunctionDecl& function, const std::string& module_path,
                            const std::string& owner) {
    const std::string path = owner.empty() ? function.name : owner + "." + function.name;
    p::FunctionDecl out{.name = function.name,
                        .generic_parameters =
                            generic_parameters(function.generic_params, function.location),
                        .parameters = {},
                        .return_type = {},
                        .body = {},
                        .attributes = attributes(function.decorators),
                        .documentation = function.doc_comment,
                        .visibility = visibility(function.visibility),
                        .is_static = owner.empty() || function.params.empty() ||
                                     function.params.front().name != "self",
                        .is_abstract = false,
                        .is_virtual = false,
                        .range = to_protocol(function.range),
                        .identity = identity(module_path, path, function.native_identity.usr)};
    for (const ParamDecl& item : function.params) out.parameters.push_back(parameter(item));
    if (has_type_ref(function.return_type_ref)) {
        out.return_type = to_protocol(function.return_type_ref);
    }
    for (const Stmt& statement : function.statements) out.body.push_back(to_protocol(statement));
    for (const Decorator& decorator : function.decorators) {
        out.is_abstract = out.is_abstract || decorator_matches(decorator, "abstract");
        out.is_virtual = out.is_virtual || decorator_matches(decorator, "virtual");
    }
    return out;
}

p::ConstantDecl to_protocol(const ConstDecl& value, const std::string& module_path,
                            const std::string& owner) {
    const std::string path = owner.empty() ? value.name : owner + "." + value.name;
    return {.name = value.name,
            .type = to_protocol(value.type_ref),
            .value = to_protocol(value.value_expr),
            .attributes = attributes(value.decorators),
            .documentation = value.doc_comment,
            .visibility = p::Visibility::Default,
            .range = to_protocol(value.location),
            .identity = identity(module_path, path)};
}

p::EnumDecl to_protocol(const EnumDecl& value, const std::string& module_path) {
    p::EnumDecl out{.name = value.name,
                    .generic_parameters = {},
                    .underlying_type = {},
                    .variants = {},
                    .attributes = attributes(value.decorators),
                    .documentation = value.doc_comment,
                    .visibility = p::Visibility::Default,
                    .range = to_protocol(value.range),
                    .identity = identity(module_path, value.name)};
    if (has_type_ref(value.underlying_type_ref)) {
        out.underlying_type = to_protocol(value.underlying_type_ref);
    }
    for (const EnumValueDecl& item : value.values) {
        out.variants.push_back(variant(item, module_path, value.name));
    }
    return out;
}

p::ClassDecl to_protocol(const ClassDecl& value, const std::string& module_path) {
    p::ClassDecl out{.name = value.name,
                     .generic_parameters = generic_parameters(value.generic_params, value.location),
                     .bases = {},
                     .fields = {},
                     .methods = {},
                     .attributes = attributes(value.decorators),
                     .documentation = value.doc_comment,
                     .visibility = visibility(value.visibility),
                     .range = to_protocol(value.range),
                     .identity = identity(module_path, value.name, value.identity.usr),
                     .constants = {},
                     .static_fields = {}};
    for (const BaseClassDecl& base : value.base_class_refs) {
        out.bases.push_back(to_protocol(base.type_ref));
    }
    for (const FieldDecl& field : value.fields) {
        out.fields.push_back(to_protocol(field, module_path, value.name));
    }
    for (const FunctionDecl& method : value.methods) {
        out.methods.push_back(to_protocol(method, module_path, value.name));
    }
    for (const ConstDecl& constant : value.constants) {
        out.constants.push_back(to_protocol(constant, module_path, value.name));
    }
    for (const ConstDecl& field : value.static_fields) {
        out.static_fields.push_back(to_protocol(field, module_path, value.name));
    }
    return out;
}

p::Declaration declaration_for_invocation(const ModuleAst& module,
                                          const Invocation& invocation) {
    const ModuleAst& unit = unit_named(module, invocation.target_module);
    p::Declaration out;
    switch (invocation.target_kind) {
    case TargetKind::Class:
        out.kind = p::DeclarationKind::Class;
        out.class_decl = to_protocol(named(unit.classes, invocation, "class"), unit.module_path);
        break;
    case TargetKind::Enum:
        out.kind = p::DeclarationKind::Enum;
        out.enum_decl = to_protocol(named(unit.enums, invocation, "enum"), unit.module_path);
        break;
    case TargetKind::Function:
        out.kind = p::DeclarationKind::Function;
        out.function_decl =
            to_protocol(named(unit.functions, invocation, "function"), unit.module_path);
        break;
    case TargetKind::Constant:
        out.kind = p::DeclarationKind::Constant;
        out.constant_decl =
            to_protocol(named(unit.constants, invocation, "constant"), unit.module_path);
        break;
    case TargetKind::Field:
        throw std::runtime_error("standalone field macro target has no owner identity");
    case TargetKind::Any:
        throw std::runtime_error("macro invocation target kind is unresolved");
    }
    return out;
}

} // namespace dudu::macro
