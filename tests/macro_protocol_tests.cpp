#include "dudu/macro/macro_protocol_generated.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace dudu::macro;
using namespace dudu::macro::protocol;

SourceRange range(std::string file, std::uint32_t line, std::uint32_t column) {
    return {.file = std::move(file),
            .start = {.line = line, .column = column, .offset = 0},
            .end = {.line = line, .column = column + 1, .offset = 1}};
}

TypeRef named_type(std::string name) {
    return {.kind = TypeKind::Named, .name = std::move(name)};
}

void test_complex_expansion_round_trip() {
    FunctionDecl generated;
    generated.name = "to_json";
    generated.parameters.push_back({.name = "self", .type = named_type("Self")});
    generated.return_type = named_type("str");
    generated.body.push_back(
        {.kind = StatementKind::Return,
         .expression = Expression{.kind = ExpressionKind::StringLiteral, .value = "{}"}});
    generated.documentation = "Generated JSON encoder.";
    generated.range = range("src/player.dd", 4, 0);

    Declaration declaration;
    declaration.kind = DeclarationKind::Function;
    declaration.function_decl = generated;

    Expansion expansion;
    expansion.members.push_back({.declaration = declaration,
                                 .origin = {.kind = OriginKind::Generated,
                                            .range = range("src/player.dd", 4, 0),
                                            .macro_name = "Json"}});
    expansion.diagnostics.push_back({.severity = DiagnosticSeverity::Note,
                                     .code = "dudu.macro.generated",
                                     .message = "generated to_json",
                                     .range = range("src/player.dd", 4, 0)});

    const std::size_t original_node_count = count_nodes(expansion);
    const std::vector<std::uint8_t> bytes = encode(expansion);
    const Expansion decoded = decode_Expansion(bytes);
    assert(decoded.members.size() == 1);
    assert(decoded.members[0].declaration.kind == DeclarationKind::Function);
    assert(decoded.members[0].declaration.function_decl.has_value());
    assert(decoded.members[0].declaration.function_decl->name == "to_json");
    assert(decoded.members[0].declaration.function_decl->body.size() == 1);
    assert(decoded.members[0].origin.macro_name == "Json");
    assert(decoded.diagnostics.size() == 1);
    assert(decoded.diagnostics[0].message == "generated to_json");
    assert(original_node_count > 10);
    assert(count_nodes(decoded) == original_node_count);
}

void test_unknown_fields_are_skipped() {
    Hello hello;
    hello.compiler_version = "0.1.0-alpha.13";
    hello.protocol_version = protocol_version;
    hello.schema_version = schema_version;
    std::vector<std::uint8_t> bytes = encode(hello);

    wire::Writer unknown;
    unknown.write_string(200, "future field");
    const auto suffix = unknown.take();
    bytes.insert(bytes.end(), suffix.begin(), suffix.end());

    const Hello decoded = decode_Hello(bytes);
    assert(decoded.compiler_version == hello.compiler_version);
    assert(decoded.protocol_version == protocol_version);
    assert(decoded.schema_version == schema_version);
}

void test_frame_round_trip_and_bounds() {
    wire::Frame frame;
    frame.protocol_version = static_cast<std::uint16_t>(protocol_version);
    frame.message_kind = static_cast<std::uint16_t>(MessageKind::Expand);
    frame.request_id = 0x1020304050607080ULL;
    frame.payload = {1, 2, 3, 4};

    const std::vector<std::uint8_t> bytes = wire::encode_frame(frame);
    const wire::Frame decoded = wire::decode_frame(bytes);
    assert(decoded.protocol_version == frame.protocol_version);
    assert(decoded.message_kind == frame.message_kind);
    assert(decoded.request_id == frame.request_id);
    assert(decoded.payload == frame.payload);

    bool rejected = false;
    try {
        wire::DecodeLimits limits;
        limits.max_frame_bytes = 3;
        (void)wire::decode_frame(bytes, limits);
    } catch (const wire::ProtocolError&) {
        rejected = true;
    }
    assert(rejected);
}

void test_truncated_payload_is_rejected() {
    std::vector<std::uint8_t> bytes = encode(Hello{.compiler_version = "dudu"});
    assert(!bytes.empty());
    bytes.pop_back();
    bool rejected = false;
    try {
        (void)decode_Hello(bytes);
    } catch (const wire::ProtocolError&) {
        rejected = true;
    }
    assert(rejected);
}

} // namespace

int main() {
    test_complex_expansion_round_trip();
    test_unknown_fields_are_skipped();
    test_frame_round_trip_and_bounds();
    test_truncated_payload_is_rejected();
    std::cout << "macro protocol tests passed\n";
    return 0;
}
