#pragma once

#include "dudu/macro/macro_protocol_generated.hpp"

#include <string>

namespace dudu::macro {

void apply_expansion_hygiene(protocol::Expansion& expansion, const std::string& macro_identity,
                             const std::string& target_module, const std::string& target_name,
                             const protocol::SourceRange& invocation);

} // namespace dudu::macro
