#include "dudu/macro/macro_worker_runtime.hpp"

#include <cassert>
#include <sstream>
#include <string>

namespace {

dudu::macro::wire::Frame frame(dudu::macro::protocol::MessageKind kind, std::uint64_t request_id,
                               std::vector<std::uint8_t> payload = {}) {
    return {.protocol_version = dudu::macro::protocol::protocol_version,
            .message_kind = static_cast<std::uint16_t>(kind),
            .request_id = request_id,
            .payload = std::move(payload)};
}

void test_persistent_worker_session() {
    namespace macro = dudu::macro;
    macro::protocol::Hello hello{.compiler_version = "test",
                                 .protocol_version = macro::protocol::protocol_version,
                                 .schema_version = macro::protocol::schema_version,
                                 .max_frame_bytes = 1024 * 1024};
    macro::protocol::ExpansionRequest request{.macro_name = "demo.Debug"};

    std::stringstream input;
    macro::write_worker_frame(
        input, frame(macro::protocol::MessageKind::Hello, 1, macro::protocol::encode(hello)));
    macro::write_worker_frame(input, frame(macro::protocol::MessageKind::Describe, 2));
    macro::write_worker_frame(
        input, frame(macro::protocol::MessageKind::Expand, 3, macro::protocol::encode(request)));
    macro::write_worker_frame(input, frame(macro::protocol::MessageKind::Shutdown, 4));

    macro::protocol::MacroCatalog catalog{.package = "demo", .binary_identity = "worker-test"};
    catalog.macros.push_back({.name = "Debug", .entry_point = "demo.Debug"});
    int calls = 0;
    std::stringstream output;
    const int status = macro::serve_worker(
        input, output, catalog, [&](const macro::protocol::ExpansionRequest& decoded) {
            ++calls;
            assert(decoded.macro_name == "demo.Debug");
            return macro::protocol::ExpansionResponse{.cacheable = true};
        });
    assert(status == 0);
    assert(calls == 1);

    const auto hello_frame = macro::read_worker_frame(output);
    const auto catalog_frame = macro::read_worker_frame(output);
    const auto expansion_frame = macro::read_worker_frame(output);
    assert(hello_frame.has_value());
    assert(catalog_frame.has_value());
    assert(expansion_frame.has_value());
    assert(!macro::read_worker_frame(output).has_value());
    assert(static_cast<macro::protocol::MessageKind>(hello_frame->message_kind) ==
           macro::protocol::MessageKind::HelloAck);
    assert(macro::protocol::decode_HelloAck(hello_frame->payload).accepted);
    assert(macro::protocol::decode_MacroCatalog(catalog_frame->payload).package == "demo");
    const macro::protocol::ExpansionResponse response =
        macro::protocol::decode_ExpansionResponse(expansion_frame->payload);
    assert(response.cacheable);
    assert(response.execute_ns > 0);
}

void test_handshake_rejects_schema_mismatch() {
    namespace macro = dudu::macro;
    macro::protocol::Hello hello{.compiler_version = "test",
                                 .protocol_version = macro::protocol::protocol_version,
                                 .schema_version = macro::protocol::schema_version + 1,
                                 .max_frame_bytes = 1024};
    std::stringstream input;
    macro::write_worker_frame(
        input, frame(macro::protocol::MessageKind::Hello, 1, macro::protocol::encode(hello)));
    std::stringstream output;
    assert(macro::serve_worker(input, output, {}, [](const auto&) {
               return macro::protocol::ExpansionResponse{};
           }) == 1);
    const auto response = macro::read_worker_frame(output);
    assert(response.has_value());
    const auto ack = macro::protocol::decode_HelloAck(response->payload);
    assert(!ack.accepted);
}

} // namespace

int main() {
    test_persistent_worker_session();
    test_handshake_rejects_schema_mismatch();
    return 0;
}
