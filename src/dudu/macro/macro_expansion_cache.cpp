#include "dudu/macro/macro_expansion_cache.hpp"

#include "dudu/macro/macro_capabilities.hpp"
#include "dudu/macro/macro_hash.hpp"

#include <atomic>
#include <fstream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace dudu::macro {
namespace {

std::atomic<std::uint64_t> staging_counter{0};

namespace p = protocol;

void clear(p::SourceRange& range) {
    range = {};
}

void normalize(p::TypeRef& type);
void normalize(p::Expression& expression);
void normalize(p::Statement& statement);

void normalize(p::Attribute& attribute) {
    clear(attribute.range);
    for (p::AttributeArgument& argument : attribute.arguments) {
        clear(argument.range);
        normalize(argument.value);
    }
}

void normalize(p::TypeRef& type) {
    clear(type.range);
    for (p::TypeRef& child : type.children)
        normalize(child);
}

void normalize(p::Expression& expression) {
    clear(expression.range);
    for (p::Expression& child : expression.children)
        normalize(child);
    for (p::Expression& child : expression.callee)
        normalize(child);
    for (p::Expression& child : expression.template_arguments)
        normalize(child);
    for (p::TypeRef& type : expression.type_arguments)
        normalize(type);
    if (expression.resolved_type)
        normalize(*expression.resolved_type);
}

void normalize(p::Statement& statement) {
    clear(statement.range);
    if (statement.type)
        normalize(*statement.type);
    for (p::Expression* expression : {statement.expression ? &*statement.expression : nullptr,
                                      statement.value ? &*statement.value : nullptr,
                                      statement.target ? &*statement.target : nullptr,
                                      statement.condition ? &*statement.condition : nullptr,
                                      statement.message ? &*statement.message : nullptr,
                                      statement.iterable ? &*statement.iterable : nullptr,
                                      statement.pattern ? &*statement.pattern : nullptr,
                                      statement.guard ? &*statement.guard : nullptr}) {
        if (expression != nullptr)
            normalize(*expression);
    }
    for (p::Statement& child : statement.children)
        normalize(child);
}

void normalize(p::GenericParameter& parameter) {
    clear(parameter.range);
    if (parameter.type)
        normalize(*parameter.type);
    if (parameter.default_type)
        normalize(*parameter.default_type);
    if (parameter.default_value)
        normalize(*parameter.default_value);
}

void normalize(p::FieldDecl& field) {
    clear(field.range);
    normalize(field.type);
    if (field.value)
        normalize(*field.value);
    for (p::Attribute& attribute : field.attributes)
        normalize(attribute);
}

void normalize(p::FunctionDecl& function) {
    clear(function.range);
    for (p::GenericParameter& parameter : function.generic_parameters)
        normalize(parameter);
    for (p::Parameter& parameter : function.parameters) {
        clear(parameter.range);
        normalize(parameter.type);
        if (parameter.default_value)
            normalize(*parameter.default_value);
    }
    if (function.return_type)
        normalize(*function.return_type);
    for (p::Statement& statement : function.body)
        normalize(statement);
    for (p::Attribute& attribute : function.attributes)
        normalize(attribute);
}

void normalize(p::ConstantDecl& constant) {
    clear(constant.range);
    normalize(constant.type);
    normalize(constant.value);
    for (p::Attribute& attribute : constant.attributes)
        normalize(attribute);
}

void normalize(p::ClassDecl& klass) {
    clear(klass.range);
    for (p::GenericParameter& parameter : klass.generic_parameters)
        normalize(parameter);
    for (p::TypeRef& base : klass.bases)
        normalize(base);
    for (p::FieldDecl& field : klass.fields)
        normalize(field);
    for (p::FunctionDecl& method : klass.methods)
        normalize(method);
    for (p::Attribute& attribute : klass.attributes)
        normalize(attribute);
    for (p::ConstantDecl& constant : klass.constants)
        normalize(constant);
    for (p::ConstantDecl& field : klass.static_fields)
        normalize(field);
}

void normalize(p::EnumDecl& en) {
    clear(en.range);
    for (p::GenericParameter& parameter : en.generic_parameters)
        normalize(parameter);
    if (en.underlying_type)
        normalize(*en.underlying_type);
    for (p::EnumVariant& variant : en.variants) {
        clear(variant.range);
        for (p::FieldDecl& field : variant.fields)
            normalize(field);
        if (variant.value)
            normalize(*variant.value);
        for (p::Attribute& attribute : variant.attributes)
            normalize(attribute);
    }
    for (p::Attribute& attribute : en.attributes)
        normalize(attribute);
}

void normalize(p::Declaration& declaration) {
    if (declaration.class_decl)
        normalize(*declaration.class_decl);
    if (declaration.enum_decl)
        normalize(*declaration.enum_decl);
    if (declaration.function_decl)
        normalize(*declaration.function_decl);
    if (declaration.field_decl)
        normalize(*declaration.field_decl);
    if (declaration.constant_decl)
        normalize(*declaration.constant_decl);
    if (declaration.implementation_decl) {
        clear(declaration.implementation_decl->range);
        normalize(declaration.implementation_decl->contract);
        normalize(declaration.implementation_decl->target);
        for (p::FunctionDecl& method : declaration.implementation_decl->methods)
            normalize(method);
    }
}

std::filesystem::path cache_path(const std::filesystem::path& directory, std::string_view key) {
    return directory / (std::string(key) + ".bin");
}

} // namespace

std::string expansion_cache_key(std::string_view worker_identity,
                                const p::ExpansionRequest& request) {
    p::ExpansionRequest canonical = request;
    clear(canonical.invocation);
    normalize(canonical.declaration);
    for (p::AttributeArgument& value : canonical.compile_values) {
        clear(value.range);
        normalize(value.value);
    }
    const std::vector<std::uint8_t> bytes = p::encode(canonical);
    StableHash hash;
    hash.add("dudu-macro-expansion-v1");
    hash.add(worker_identity);
    hash.add_bytes(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    return hash.finish();
}

std::optional<p::ExpansionResponse> read_expansion_cache(const std::filesystem::path& directory,
                                                         std::string_view key) {
    const std::filesystem::path path = cache_path(directory, key);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                          std::istreambuf_iterator<char>());
    try {
        p::ExpansionResponse response = p::decode_ExpansionResponse(bytes);
        if (!external_inputs_are_current(response.external_input_hashes))
            return std::nullopt;
        return response;
    } catch (const std::exception&) {
        std::error_code error;
        std::filesystem::remove(path, error);
        return std::nullopt;
    }
}

void write_expansion_cache(const std::filesystem::path& directory, std::string_view key,
                           const p::ExpansionResponse& response) {
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = cache_path(directory, key);
    const std::filesystem::path staging =
        path.string() + ".tmp." + std::to_string(::getpid()) + "." +
        std::to_string(staging_counter.fetch_add(1, std::memory_order_relaxed));
    const std::vector<std::uint8_t> bytes = p::encode(response);
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("could not write macro expansion cache");
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    std::error_code error;
    std::filesystem::rename(staging, path, error);
    if (error && !std::filesystem::is_regular_file(path)) {
        std::filesystem::remove(staging);
        throw std::runtime_error("could not publish macro expansion cache: " + error.message());
    }
    if (error)
        std::filesystem::remove(staging);
}

} // namespace dudu::macro
