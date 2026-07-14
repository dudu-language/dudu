#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace dudu::macro::wire {

enum class FieldType : std::uint8_t {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    Fixed32 = 5,
};

struct DecodeLimits {
    std::size_t max_frame_bytes = 64U * 1024U * 1024U;
    std::size_t max_string_bytes = 8U * 1024U * 1024U;
    // Allows the one-million-public-node expansion budget plus wire envelopes.
    std::size_t max_nodes = 1'100'000U;
    std::size_t max_depth = 256U;
};

class ProtocolError : public std::runtime_error {
  public:
    explicit ProtocolError(const std::string& message) : std::runtime_error(message) {
    }
};

class Writer {
  public:
    void write_varint(std::uint32_t tag, std::uint64_t value);
    void write_signed(std::uint32_t tag, std::int64_t value);
    void write_double(std::uint32_t tag, double value);
    void write_string(std::uint32_t tag, const std::string& value);
    void write_bytes(std::uint32_t tag, std::span<const std::uint8_t> value);
    void write_message(std::uint32_t tag, const std::function<void(Writer&)>& encode);

    std::span<const std::uint8_t> bytes() const {
        return bytes_;
    }
    std::vector<std::uint8_t> take() {
        return std::move(bytes_);
    }

  private:
    void write_key(std::uint32_t tag, FieldType type);
    void write_raw_varint(std::uint64_t value);
    void write_length(std::size_t size);

    std::vector<std::uint8_t> bytes_;
};

class Reader {
  public:
    static Reader root(std::span<const std::uint8_t> bytes, const DecodeLimits& limits);

    bool next(std::uint32_t& tag, FieldType& type);
    std::uint64_t read_varint(FieldType type);
    std::uint32_t read_u32(FieldType type);
    std::int64_t read_signed(FieldType type);
    double read_double(FieldType type);
    std::string read_string(FieldType type);
    std::vector<std::uint8_t> read_bytes(FieldType type);
    Reader read_message(FieldType type);
    void skip(FieldType type);

  private:
    struct State {
        DecodeLimits limits;
        std::size_t nodes = 0;
    };

    Reader(std::span<const std::uint8_t> bytes, std::shared_ptr<State> state, std::size_t depth);

    void require_type(FieldType actual, FieldType expected) const;
    std::uint64_t read_raw_varint();
    std::span<const std::uint8_t> read_length_delimited();
    void require(std::size_t count) const;

    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0;
    std::shared_ptr<State> state_;
    std::size_t depth_ = 0;
};

struct Frame {
    std::uint16_t protocol_version = 1;
    std::uint16_t message_kind = 0;
    std::uint64_t request_id = 0;
    std::vector<std::uint8_t> payload;
};

inline constexpr std::uint32_t frame_magic = 0x55445544U; // "DUDU" little-endian.
inline constexpr std::size_t frame_header_bytes = 20U;

std::vector<std::uint8_t> encode_frame(const Frame& frame);
Frame decode_frame(std::span<const std::uint8_t> bytes, const DecodeLimits& limits = {});

} // namespace dudu::macro::wire
