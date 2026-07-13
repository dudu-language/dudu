#pragma once

#include "dudu/macro/macro_protocol_generated.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace dudu::macro {

std::string expansion_cache_key(std::string_view worker_identity,
                                const protocol::ExpansionRequest& request);
std::optional<protocol::ExpansionResponse>
read_expansion_cache(const std::filesystem::path& directory, std::string_view key);
void write_expansion_cache(const std::filesystem::path& directory, std::string_view key,
                           const protocol::ExpansionResponse& response);

} // namespace dudu::macro
