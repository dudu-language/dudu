#pragma once

#include <iosfwd>

namespace dudu {

int run_language_server(std::istream& in, std::ostream& out, std::ostream& err);

} // namespace dudu
