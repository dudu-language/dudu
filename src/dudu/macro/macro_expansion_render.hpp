#pragma once

#include "dudu/macro/macro_expansion.hpp"

#include <string>

namespace dudu::macro {

struct ExpansionRenderOptions {
    std::string macro_filter;
    bool show_origins = false;
};

std::string render_expansion_report(const ExpansionReport& report,
                                    const ExpansionRenderOptions& options = {});

} // namespace dudu::macro
