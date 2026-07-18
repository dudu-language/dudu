#pragma once

#include <string>

namespace string_reference_sink {

class Sink {
  public:
    bool write(const std::string& value) const { return value == "dudu"; }
};

} // namespace string_reference_sink
