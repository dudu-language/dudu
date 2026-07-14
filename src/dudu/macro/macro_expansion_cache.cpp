#include "dudu/macro/macro_expansion_cache.hpp"

#include "dudu/macro/macro_capabilities.hpp"
#include "dudu/macro/macro_hash.hpp"

#include <atomic>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <unordered_map>

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
    for (p::FunctionDecl& method : en.methods)
        normalize(method);
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

constexpr std::string_view batch_magic = "DUDUMCB2";

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::optional<std::uint64_t> read_u64(std::span<const std::uint8_t> bytes, std::size_t& offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint64_t))
        return std::nullopt;
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    return value;
}

std::string batch_cache_key(std::span<const std::string> keys) {
    StableHash hash;
    hash.add("dudu-macro-expansion-batch-v2");
    for (const std::string& key : keys)
        hash.add(key);
    return hash.finish();
}

std::filesystem::path batch_cache_path(const std::filesystem::path& directory,
                                       std::span<const std::string> keys) {
    return cache_path(directory / "batches", batch_cache_key(keys));
}

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return std::nullopt;
    const std::streamoff size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) >
                        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input)
        return std::nullopt;
    return bytes;
}

void publish_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path staging =
        path.string() + ".tmp." + std::to_string(::getpid()) + "." +
        std::to_string(staging_counter.fetch_add(1, std::memory_order_relaxed));
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
    const std::filesystem::path path = cache_path(directory, key);
    const std::vector<std::uint8_t> bytes = p::encode(response);
    publish_file(path, bytes);
}

std::vector<CachedExpansionResponse> read_expansion_caches(const std::filesystem::path& directory,
                                                           std::span<const std::string> keys,
                                                           bool* batch_hit) {
    if (batch_hit != nullptr)
        *batch_hit = false;
    std::vector<CachedExpansionResponse> responses(keys.size());
    if (keys.size() > 1) {
        const std::filesystem::path path = batch_cache_path(directory, keys);
        if (const auto bytes = read_file(path)) {
            try {
                std::size_t offset = 0;
                if (bytes->size() >= batch_magic.size() &&
                    std::memcmp(bytes->data(), batch_magic.data(), batch_magic.size()) == 0) {
                    offset += batch_magic.size();
                    const auto count = read_u64(*bytes, offset);
                    if (count && *count == keys.size()) {
                        bool valid = true;
                        const auto unique_count = read_u64(*bytes, offset);
                        if (!unique_count || *unique_count > keys.size())
                            valid = false;
                        std::vector<CachedExpansionResponse> unique_responses;
                        if (valid)
                            unique_responses.reserve(static_cast<std::size_t>(*unique_count));
                        for (std::size_t index = 0; valid && index < *unique_count; ++index) {
                            const auto size = read_u64(*bytes, offset);
                            if (!size || *size > bytes->size() - offset) {
                                valid = false;
                                break;
                            }
                            auto response = std::make_shared<p::ExpansionResponse>(
                                p::decode_ExpansionResponse(std::span(*bytes).subspan(
                                    offset, static_cast<std::size_t>(*size))));
                            offset += static_cast<std::size_t>(*size);
                            if (!external_inputs_are_current(response->external_input_hashes)) {
                                valid = false;
                                break;
                            }
                            unique_responses.push_back(std::move(response));
                        }
                        for (std::size_t index = 0; valid && index < keys.size(); ++index) {
                            const auto response_index = read_u64(*bytes, offset);
                            if (!response_index || *response_index >= unique_responses.size()) {
                                valid = false;
                                break;
                            }
                            responses[index] = unique_responses[*response_index];
                        }
                        if (valid && offset == bytes->size()) {
                            if (batch_hit != nullptr)
                                *batch_hit = true;
                            return responses;
                        }
                    }
                }
            } catch (const std::exception&) {
            }
            std::error_code error;
            std::filesystem::remove(path, error);
            responses.assign(keys.size(), nullptr);
        }
    }
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (auto response = read_expansion_cache(directory, keys[index]))
            responses[index] = std::make_shared<p::ExpansionResponse>(std::move(*response));
    }
    return responses;
}

void write_expansion_cache_batch(const std::filesystem::path& directory,
                                 std::span<const std::string> keys,
                                 std::span<const CachedExpansionResponse> responses) {
    if (keys.size() <= 1 || keys.size() != responses.size())
        return;
    std::vector<std::string> unique_payloads;
    std::vector<std::uint64_t> response_indices;
    std::unordered_map<std::string, std::uint64_t> payload_indices;
    unique_payloads.reserve(responses.size());
    response_indices.reserve(responses.size());
    payload_indices.reserve(responses.size());
    for (const auto& response : responses) {
        if (!response || !response->cacheable)
            return;
        const std::vector<std::uint8_t> encoded = p::encode(*response);
        std::string payload(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        const auto found = payload_indices.find(payload);
        if (found != payload_indices.end()) {
            response_indices.push_back(found->second);
            continue;
        }
        const std::uint64_t index = unique_payloads.size();
        payload_indices.emplace(payload, index);
        unique_payloads.push_back(std::move(payload));
        response_indices.push_back(index);
    }
    std::size_t total_size = batch_magic.size() + 2 * sizeof(std::uint64_t) +
                             response_indices.size() * sizeof(std::uint64_t);
    for (const std::string& payload : unique_payloads)
        total_size += sizeof(std::uint64_t) + payload.size();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(total_size);
    bytes.insert(bytes.end(), batch_magic.begin(), batch_magic.end());
    append_u64(bytes, responses.size());
    append_u64(bytes, unique_payloads.size());
    for (const std::string& payload : unique_payloads) {
        append_u64(bytes, payload.size());
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    for (const std::uint64_t index : response_indices)
        append_u64(bytes, index);
    publish_file(batch_cache_path(directory, keys), bytes);
}

} // namespace dudu::macro
