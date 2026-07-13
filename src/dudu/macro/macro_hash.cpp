#include "dudu/macro/macro_hash.hpp"

#include <iomanip>
#include <sstream>

namespace dudu::macro {

void StableHash::add(std::string_view value) {
    mix_u64(value.size());
    add_bytes(value);
}

void StableHash::add_bytes(std::string_view value) {
    for (const unsigned char byte : value) {
        mix(byte);
    }
}

std::string StableHash::finish() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << first_ << std::setw(16) << second_;
    return out.str();
}

void StableHash::mix(unsigned char byte) {
    first_ = (first_ ^ byte) * 1099511628211ULL;
    second_ = (second_ + byte + 0x9e3779b97f4a7c15ULL) * 14029467366897019727ULL;
}

void StableHash::mix_u64(std::uint64_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        mix(static_cast<unsigned char>((value >> (index * 8U)) & 0xffU));
    }
}

} // namespace dudu::macro
