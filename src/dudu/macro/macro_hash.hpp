#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dudu::macro {

class StableHash {
  public:
    void add(std::string_view value);
    void add_bytes(std::string_view value);
    std::string finish() const;

  private:
    void mix(unsigned char byte);
    void mix_u64(std::uint64_t value);

    std::uint64_t first_ = 14695981039346656037ULL;
    std::uint64_t second_ = 1099511628211ULL;
};

} // namespace dudu::macro
