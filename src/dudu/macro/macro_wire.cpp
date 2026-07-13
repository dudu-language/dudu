#include "dudu/macro/macro_wire.hpp"

#include <bit>
#include <cstring>
#include <limits>

namespace dudu::macro::wire {
namespace {

template <typename T> void append_little(std::vector<std::uint8_t>& out, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU));
    }
}

template <typename T> T read_little(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        throw ProtocolError("truncated macro protocol frame header");
    }
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(bytes[offset + i]) << (i * 8U);
    }
    return value;
}

} // namespace

void Writer::write_key(std::uint32_t tag, FieldType type) {
    if (tag == 0 || tag > (std::numeric_limits<std::uint32_t>::max() >> 3U)) {
        throw ProtocolError("invalid macro protocol field tag");
    }
    write_raw_varint((static_cast<std::uint64_t>(tag) << 3U) | static_cast<std::uint8_t>(type));
}

void Writer::write_raw_varint(std::uint64_t value) {
    while (value >= 0x80U) {
        bytes_.push_back(static_cast<std::uint8_t>(value) | 0x80U);
        value >>= 7U;
    }
    bytes_.push_back(static_cast<std::uint8_t>(value));
}

void Writer::write_length(std::size_t size) {
    write_raw_varint(static_cast<std::uint64_t>(size));
}

void Writer::write_varint(std::uint32_t tag, std::uint64_t value) {
    write_key(tag, FieldType::Varint);
    write_raw_varint(value);
}

void Writer::write_signed(std::uint32_t tag, std::int64_t value) {
    const std::uint64_t encoded =
        (static_cast<std::uint64_t>(value) << 1U) ^ static_cast<std::uint64_t>(value >> 63U);
    write_varint(tag, encoded);
}

void Writer::write_double(std::uint32_t tag, double value) {
    write_key(tag, FieldType::Fixed64);
    append_little(bytes_, std::bit_cast<std::uint64_t>(value));
}

void Writer::write_string(std::uint32_t tag, const std::string& value) {
    write_bytes(tag, std::span<const std::uint8_t>(
                         reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

void Writer::write_bytes(std::uint32_t tag, std::span<const std::uint8_t> value) {
    write_key(tag, FieldType::LengthDelimited);
    write_length(value.size());
    bytes_.insert(bytes_.end(), value.begin(), value.end());
}

void Writer::write_message(std::uint32_t tag, const std::function<void(Writer&)>& encode) {
    Writer nested;
    encode(nested);
    write_bytes(tag, nested.bytes());
}

Reader::Reader(std::span<const std::uint8_t> bytes, std::shared_ptr<State> state, std::size_t depth)
    : bytes_(bytes), state_(std::move(state)), depth_(depth) {
    if (depth_ > state_->limits.max_depth) {
        throw ProtocolError("macro protocol nesting limit exceeded");
    }
    ++state_->nodes;
    if (state_->nodes > state_->limits.max_nodes) {
        throw ProtocolError("macro protocol node limit exceeded");
    }
}

Reader Reader::root(std::span<const std::uint8_t> bytes, const DecodeLimits& limits) {
    if (bytes.size() > limits.max_frame_bytes) {
        throw ProtocolError("macro protocol payload exceeds size limit");
    }
    auto state = std::make_shared<State>();
    state->limits = limits;
    return Reader(bytes, std::move(state), 0);
}

void Reader::require(std::size_t count) const {
    if (cursor_ > bytes_.size() || count > bytes_.size() - cursor_) {
        throw ProtocolError("truncated macro protocol payload");
    }
}

std::uint64_t Reader::read_raw_varint() {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64U; shift += 7U) {
        require(1);
        const std::uint8_t byte = bytes_[cursor_++];
        value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            return value;
        }
    }
    throw ProtocolError("macro protocol varint overflow");
}

bool Reader::next(std::uint32_t& tag, FieldType& type) {
    if (cursor_ == bytes_.size()) {
        return false;
    }
    const std::uint64_t key = read_raw_varint();
    tag = static_cast<std::uint32_t>(key >> 3U);
    type = static_cast<FieldType>(key & 0x7U);
    if (tag == 0) {
        throw ProtocolError("macro protocol field tag zero is invalid");
    }
    return true;
}

void Reader::require_type(FieldType actual, FieldType expected) const {
    if (actual != expected) {
        throw ProtocolError("macro protocol field has the wrong wire type");
    }
}

std::uint64_t Reader::read_varint(FieldType type) {
    require_type(type, FieldType::Varint);
    return read_raw_varint();
}

std::uint32_t Reader::read_u32(FieldType type) {
    const std::uint64_t value = read_varint(type);
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw ProtocolError("macro protocol u32 overflow");
    }
    return static_cast<std::uint32_t>(value);
}

std::int64_t Reader::read_signed(FieldType type) {
    const std::uint64_t value = read_varint(type);
    return static_cast<std::int64_t>((value >> 1U) ^ (~(value & 1U) + 1U));
}

double Reader::read_double(FieldType type) {
    require_type(type, FieldType::Fixed64);
    require(sizeof(std::uint64_t));
    const std::uint64_t bits = read_little<std::uint64_t>(bytes_, cursor_);
    cursor_ += sizeof(std::uint64_t);
    return std::bit_cast<double>(bits);
}

std::span<const std::uint8_t> Reader::read_length_delimited() {
    const std::uint64_t raw_length = read_raw_varint();
    if (raw_length > std::numeric_limits<std::size_t>::max()) {
        throw ProtocolError("macro protocol length overflow");
    }
    const std::size_t length = static_cast<std::size_t>(raw_length);
    require(length);
    const std::span<const std::uint8_t> value = bytes_.subspan(cursor_, length);
    cursor_ += length;
    return value;
}

std::string Reader::read_string(FieldType type) {
    require_type(type, FieldType::LengthDelimited);
    const auto value = read_length_delimited();
    if (value.size() > state_->limits.max_string_bytes) {
        throw ProtocolError("macro protocol string exceeds size limit");
    }
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::vector<std::uint8_t> Reader::read_bytes(FieldType type) {
    require_type(type, FieldType::LengthDelimited);
    const auto value = read_length_delimited();
    if (value.size() > state_->limits.max_string_bytes) {
        throw ProtocolError("macro protocol byte field exceeds size limit");
    }
    return {value.begin(), value.end()};
}

Reader Reader::read_message(FieldType type) {
    require_type(type, FieldType::LengthDelimited);
    return Reader(read_length_delimited(), state_, depth_ + 1U);
}

void Reader::skip(FieldType type) {
    switch (type) {
    case FieldType::Varint:
        (void)read_raw_varint();
        return;
    case FieldType::Fixed64:
        require(8);
        cursor_ += 8;
        return;
    case FieldType::LengthDelimited:
        (void)read_length_delimited();
        return;
    case FieldType::Fixed32:
        require(4);
        cursor_ += 4;
        return;
    }
    throw ProtocolError("unsupported macro protocol wire type");
}

std::vector<std::uint8_t> encode_frame(const Frame& frame) {
    if (frame.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw ProtocolError("macro protocol frame payload is too large");
    }
    std::vector<std::uint8_t> out;
    out.reserve(frame_header_bytes + frame.payload.size());
    append_little(out, frame_magic);
    append_little(out, frame.protocol_version);
    append_little(out, frame.message_kind);
    append_little(out, frame.request_id);
    append_little(out, static_cast<std::uint32_t>(frame.payload.size()));
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    return out;
}

Frame decode_frame(std::span<const std::uint8_t> bytes, const DecodeLimits& limits) {
    if (bytes.size() < frame_header_bytes) {
        throw ProtocolError("truncated macro protocol frame");
    }
    if (read_little<std::uint32_t>(bytes, 0) != frame_magic) {
        throw ProtocolError("invalid macro protocol frame magic");
    }
    const std::uint32_t payload_size = read_little<std::uint32_t>(bytes, 16);
    if (payload_size > limits.max_frame_bytes) {
        throw ProtocolError("macro protocol frame exceeds size limit");
    }
    if (bytes.size() != frame_header_bytes + payload_size) {
        throw ProtocolError("macro protocol frame length mismatch");
    }
    Frame frame;
    frame.protocol_version = read_little<std::uint16_t>(bytes, 4);
    frame.message_kind = read_little<std::uint16_t>(bytes, 6);
    frame.request_id = read_little<std::uint64_t>(bytes, 8);
    frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(frame_header_bytes),
                         bytes.end());
    return frame;
}

} // namespace dudu::macro::wire
