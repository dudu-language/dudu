#pragma once

#include "dudu/macro/macro_protocol_generated.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dudu::macro {

struct CapabilityOutcome {
    bool deterministic = true;
    std::vector<std::string> external_input_hashes;
};

void begin_capability_scope(const std::vector<protocol::Capability>& capabilities,
                            const std::filesystem::path& project_root);
CapabilityOutcome finish_capability_scope();
void discard_capability_scope() noexcept;

bool external_inputs_are_current(const std::vector<std::string>& records);

namespace host {

std::string read_text(const std::string& path);
void write_text(const std::string& path, const std::string& contents);
std::string read_env(const std::string& name);
int run(const std::string& command);
void require_network(const std::string& endpoint);
std::uint64_t clock_ns();
std::uint64_t random_u64();

} // namespace host
} // namespace dudu::macro
