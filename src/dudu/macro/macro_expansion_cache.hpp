#pragma once

#include "dudu/macro/macro_protocol_generated.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dudu::macro {

using CachedExpansionResponse = std::shared_ptr<protocol::ExpansionResponse>;

std::string expansion_cache_key(std::string_view worker_identity,
                                const protocol::ExpansionRequest& request);
std::optional<protocol::ExpansionResponse>
read_expansion_cache(const std::filesystem::path& directory, std::string_view key);
void write_expansion_cache(const std::filesystem::path& directory, std::string_view key,
                           const protocol::ExpansionResponse& response);
std::vector<CachedExpansionResponse> read_expansion_caches(const std::filesystem::path& directory,
                                                           std::span<const std::string> keys,
                                                           bool* batch_hit = nullptr);
void write_expansion_cache_batch(const std::filesystem::path& directory,
                                 std::span<const std::string> keys,
                                 std::span<const CachedExpansionResponse> responses);

} // namespace dudu::macro
