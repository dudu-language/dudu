#include "dudu/macro/macro_worker_runtime.hpp"

#include "dudu/macro/macro_capabilities.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace dudu::macro {
namespace {

std::uint32_t little_u32(const std::array<std::uint8_t, wire::frame_header_bytes>& header,
                         std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(header[offset + index]) << (index * 8U);
    }
    return value;
}

wire::Frame response_frame(const wire::Frame& request, protocol::MessageKind kind,
                           std::vector<std::uint8_t> payload = {}) {
    return {.protocol_version = static_cast<std::uint16_t>(protocol::protocol_version),
            .message_kind = static_cast<std::uint16_t>(kind),
            .request_id = request.request_id,
            .payload = std::move(payload)};
}

protocol::WorkerError worker_error(std::string code, std::string message) {
    return {.code = std::move(code), .message = std::move(message), .diagnostics = {}};
}

void write_error(std::ostream& output, const wire::Frame& request, std::string code,
                 std::string message) {
    write_worker_frame(output, response_frame(request, protocol::MessageKind::WorkerError,
                                              protocol::encode(worker_error(std::move(code),
                                                                            std::move(message)))));
}

} // namespace

std::optional<wire::Frame> read_worker_frame(std::istream& input,
                                             const wire::DecodeLimits& limits) {
    std::array<std::uint8_t, wire::frame_header_bytes> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const std::streamsize header_bytes = input.gcount();
    if (header_bytes == 0 && input.eof()) {
        return std::nullopt;
    }
    if (header_bytes != static_cast<std::streamsize>(header.size())) {
        throw wire::ProtocolError("truncated macro worker frame header");
    }
    const std::uint32_t payload_size = little_u32(header, 16);
    if (payload_size > limits.max_frame_bytes) {
        throw wire::ProtocolError("macro worker frame exceeds size limit");
    }
    std::vector<std::uint8_t> bytes(header.begin(), header.end());
    bytes.resize(header.size() + payload_size);
    if (payload_size != 0) {
        input.read(reinterpret_cast<char*>(bytes.data() + header.size()),
                   static_cast<std::streamsize>(payload_size));
        if (input.gcount() != static_cast<std::streamsize>(payload_size)) {
            throw wire::ProtocolError("truncated macro worker frame payload");
        }
    }
    return wire::decode_frame(bytes, limits);
}

void write_worker_frame(std::ostream& output, const wire::Frame& frame) {
    const std::vector<std::uint8_t> bytes = wire::encode_frame(frame);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
        throw wire::ProtocolError("could not write macro worker frame");
    }
}

int serve_worker(std::istream& input, std::ostream& output, const protocol::MacroCatalog& catalog,
                 const std::filesystem::path& project_root, const ExpansionDispatch& dispatch,
                 const wire::DecodeLimits& limits) {
    bool negotiated = false;
    while (const std::optional<wire::Frame> request = read_worker_frame(input, limits)) {
        const auto kind = static_cast<protocol::MessageKind>(request->message_kind);
        try {
            if (!negotiated) {
                if (kind != protocol::MessageKind::Hello) {
                    write_error(output, *request, "dudu.macro.handshake",
                                "macro worker expected Hello as its first message");
                    return 1;
                }
                const protocol::Hello hello = protocol::decode_Hello(request->payload, limits);
                const bool accepted = hello.protocol_version == protocol::protocol_version &&
                                      hello.schema_version == protocol::schema_version;
                const protocol::HelloAck ack = {
                    .worker_version = catalog.binary_identity,
                    .protocol_version = protocol::protocol_version,
                    .schema_version = protocol::schema_version,
                    .accepted = accepted,
                    .reason = accepted ? "" : "macro protocol or AST schema version mismatch"};
                write_worker_frame(output, response_frame(*request, protocol::MessageKind::HelloAck,
                                                          protocol::encode(ack)));
                if (!accepted) {
                    return 1;
                }
                negotiated = true;
                continue;
            }
            if (kind == protocol::MessageKind::Describe) {
                write_worker_frame(output, response_frame(*request, protocol::MessageKind::Catalog,
                                                          protocol::encode(catalog)));
                continue;
            }
            if (kind == protocol::MessageKind::Expand) {
                const protocol::ExpansionRequest expansion_request =
                    protocol::decode_ExpansionRequest(request->payload, limits);
                begin_capability_scope(catalog.capabilities, project_root);
                protocol::ExpansionResponse response;
                try {
                    const auto execute_start = std::chrono::steady_clock::now();
                    response = dispatch(expansion_request);
                    response.execute_ns = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - execute_start)
                            .count());
                    CapabilityOutcome outcome = finish_capability_scope();
                    if (!outcome.deterministic && response.cacheable) {
                        throw std::runtime_error(
                            "macro used a non-cacheable capability but is not listed in "
                            "[macro].allow_non_cacheable");
                    }
                    response.cacheable = response.cacheable && outcome.deterministic;
                    response.external_input_hashes = std::move(outcome.external_input_hashes);
                } catch (...) {
                    discard_capability_scope();
                    throw;
                }
                write_worker_frame(output,
                                   response_frame(*request, protocol::MessageKind::ExpansionResult,
                                                  protocol::encode(response)));
                continue;
            }
            if (kind == protocol::MessageKind::Shutdown) {
                return 0;
            }
            write_error(output, *request, "dudu.macro.message",
                        "unsupported macro worker message kind");
        } catch (const std::exception& error) {
            write_error(output, *request, "dudu.macro.worker", error.what());
        }
    }
    return negotiated ? 0 : 1;
}

int serve_worker(std::istream& input, std::ostream& output, const protocol::MacroCatalog& catalog,
                 const ExpansionDispatch& dispatch, const wire::DecodeLimits& limits) {
    return serve_worker(input, output, catalog, std::filesystem::current_path(), dispatch, limits);
}

int serve_worker(const protocol::MacroCatalog& catalog, const ExpansionDispatch& dispatch,
                 const wire::DecodeLimits& limits) {
    return serve_worker(std::cin, std::cout, catalog, dispatch, limits);
}

int serve_worker(const protocol::MacroCatalog& catalog, const std::filesystem::path& project_root,
                 const ExpansionDispatch& dispatch, const wire::DecodeLimits& limits) {
    return serve_worker(std::cin, std::cout, catalog, project_root, dispatch, limits);
}

} // namespace dudu::macro
