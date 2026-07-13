#pragma once

#include "dudu/macro/macro_expansion.hpp"

#include <string>

namespace dudu::macro {

std::string render_expansion_report(const ExpansionReport& report);

} // namespace dudu::macro
