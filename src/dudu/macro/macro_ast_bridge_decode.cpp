#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/project/module_names.hpp"

#include <algorithm>
#include <stdexcept>

namespace dudu::macro {
namespace {

namespace p = protocol;

Visibility visibility(p::Visibility value) {
    return value == p::Visibility::Private ? Visibility::Private : Visibility::Default;
}

std::vector<Decorator> attributes(const std::vector<p::Attribute>& values,
                                  SourceLocation fallback) {
    std::vector<Decorator> out;
    out.reserve(values.size());
    for (const p::Attribute& value : values)
        out.push_back(from_protocol(value, fallback));
    return out;
}

void add_marker(std::vector<Decorator>& values, std::string_view name, SourceLocation location) {
    if (std::any_of(values.begin(), values.end(),
                    [&](const Decorator& value) { return decorator_matches(value, name); })) {
        return;
    }
    p::Attribute attribute{
        .name = std::string(name), .arguments = {}, .range = to_protocol(location), .identity = {}};
    values.push_back(from_protocol(attribute, location));
}

std::vector<std::string> generic_parameters(const std::vector<p::GenericParameter>& values) {
    std::vector<std::string> out;
    out.reserve(values.size());
    for (const p::GenericParameter& value : values) {
        if (value.name.empty()) {
            throw std::runtime_error("generated generic parameter name cannot be empty");
        }
        out.push_back(value.name + (value.variadic ? "..." : ""));
    }
    return out;
}

ParamDecl parameter(const p::Parameter& value, SourceLocation fallback) {
    const SourceRange range = from_protocol(value.range, fallback);
    return {.name = value.name,
            .type_ref = from_protocol(value.type, range.start),
            .location = range.start,
            .variadic = value.variadic};
}

EnumValueDecl variant(const p::EnumVariant& value, SourceLocation fallback) {
    const SourceRange range = from_protocol(value.range, fallback);
    EnumValueDecl out{.name = value.name,
                      .value_expr = {},
                      .payload_fields = {},
                      .decorators = attributes(value.attributes, range.start),
                      .location = range.start,
                      .doc_comment = {}};
    if (value.value)
        out.value_expr = from_protocol(*value.value, range.start);
    for (const p::FieldDecl& field : value.fields) {
        const FieldDecl converted = from_protocol(field, range.start);
        out.payload_fields.push_back({.name = converted.name,
                                      .type_ref = converted.type_ref,
                                      .decorators = converted.decorators,
                                      .location = converted.location});
    }
    return out;
}

} // namespace

FieldDecl from_protocol(const p::FieldDecl& field, SourceLocation fallback) {
    const SourceRange range = from_protocol(field.range, fallback);
    FieldDecl out{.name = field.name,
                  .type_ref = from_protocol(field.type, range.start),
                  .value_expr = {},
                  .decorators = attributes(field.attributes, range.start),
                  .location = range.start,
                  .doc_comment = field.documentation};
    if (field.value)
        out.value_expr = from_protocol(*field.value, range.start);
    return out;
}

FunctionDecl from_protocol(const p::FunctionDecl& function, const std::string& module_path,
                           const std::string& owner, SourceLocation fallback) {
    const SourceRange range = from_protocol(function.range, fallback);
    const std::string generated_path = owner.empty() ? function.name : owner + "_" + function.name;
    FunctionDecl out{.visibility = visibility(function.visibility),
                     .name = function.name,
                     .cpp_name = generated_value_name(module_path, generated_path),
                     .native_identity = {},
                     .receiver_type_ref = {},
                     .generic_params = generic_parameters(function.generic_parameters),
                     .generic_param_is_value = {},
                     .generic_default_args = {},
                     .decorators = attributes(function.attributes, range.start),
                     .params = {},
                     .return_type_ref = {},
                     .origin_module = module_path,
                     .statements = {},
                     .deleted = false,
                     .body_syntax_damaged = false,
                     .location = range.start,
                     .range = range,
                     .doc_comment = function.documentation};
    for (const p::Parameter& item : function.parameters) {
        out.params.push_back(parameter(item, range.start));
    }
    if (function.return_type) {
        out.return_type_ref = from_protocol(*function.return_type, range.start);
    }
    for (const p::Statement& statement : function.body) {
        out.statements.push_back(from_protocol(statement, range.start));
    }
    if (!function.is_static && !out.params.empty() && out.params.front().name == "self") {
        out.receiver_type_ref =
            wrapped_type_ref(TypeKind::Reference, named_type_ref("Self", range.start), range.start);
    }
    if (function.is_abstract)
        add_marker(out.decorators, "abstract", range.start);
    if (function.is_virtual)
        add_marker(out.decorators, "virtual", range.start);
    return out;
}

ConstDecl from_protocol(const p::ConstantDecl& value, const std::string& module_path,
                        const std::string& owner, SourceLocation fallback) {
    const SourceRange range = from_protocol(value.range, fallback);
    const std::string generated_path = owner.empty() ? value.name : owner + "_" + value.name;
    return {.name = value.name,
            .cpp_name = generated_value_name(module_path, generated_path),
            .type_ref = from_protocol(value.type, range.start),
            .value_expr = from_protocol(value.value, range.start),
            .decorators = attributes(value.attributes, range.start),
            .origin_module = module_path,
            .location = range.start,
            .doc_comment = value.documentation};
}

EnumDecl from_protocol(const p::EnumDecl& value, const std::string& module_path,
                       SourceLocation fallback) {
    const SourceRange range = from_protocol(value.range, fallback);
    EnumDecl out{.name = value.name,
                 .cpp_name = generated_type_name(module_path, value.name),
                 .underlying_type_ref = {},
                 .origin_module = module_path,
                 .values = {},
                 .methods = {},
                 .decorators = attributes(value.attributes, range.start),
                 .location = range.start,
                 .range = range,
                 .doc_comment = value.documentation};
    if (value.underlying_type) {
        out.underlying_type_ref = from_protocol(*value.underlying_type, range.start);
    }
    for (const p::EnumVariant& item : value.variants) {
        out.values.push_back(variant(item, range.start));
    }
    for (const p::FunctionDecl& method : value.methods) {
        out.methods.push_back(from_protocol(method, module_path, value.name, range.start));
    }
    return out;
}

ClassDecl from_protocol(const p::ClassDecl& value, const std::string& module_path,
                        SourceLocation fallback) {
    const SourceRange range = from_protocol(value.range, fallback);
    ClassDecl out{.visibility = visibility(value.visibility),
                  .name = value.name,
                  .cpp_name = generated_type_name(module_path, value.name),
                  .identity = {},
                  .layout = {},
                  .native_declaration = false,
                  .generic_params = generic_parameters(value.generic_parameters),
                  .generic_min_args = {},
                  .generic_default_args = {},
                  .native_specialization_args = {},
                  .native_specialization_requirements = {},
                  .native_partial_specialization = false,
                  .base_class_refs = {},
                  .type_aliases = {},
                  .decorators = attributes(value.attributes, range.start),
                  .fields = {},
                  .constants = {},
                  .static_fields = {},
                  .methods = {},
                  .origin_module = module_path,
                  .location = range.start,
                  .range = range,
                  .doc_comment = value.documentation};
    for (const p::TypeRef& base : value.bases) {
        out.base_class_refs.push_back(
            {.type_ref = from_protocol(base, range.start), .location = range.start});
    }
    for (const p::FieldDecl& field : value.fields) {
        out.fields.push_back(from_protocol(field, range.start));
    }
    for (const p::FunctionDecl& method : value.methods) {
        out.methods.push_back(from_protocol(method, module_path, value.name, range.start));
    }
    for (const p::ConstantDecl& constant : value.constants) {
        out.constants.push_back(from_protocol(constant, module_path, value.name, range.start));
    }
    for (const p::ConstantDecl& field : value.static_fields) {
        out.static_fields.push_back(from_protocol(field, module_path, value.name, range.start));
    }
    return out;
}

} // namespace dudu::macro
