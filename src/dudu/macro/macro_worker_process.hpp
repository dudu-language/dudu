#pragma once

#include "dudu/macro/macro_protocol_generated.hpp"
#include "dudu/macro/macro_wire.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace dudu::macro {

struct WorkerProcessOptions {
    std::chrono::milliseconds request_timeout{5000};
    wire::DecodeLimits decode_limits{};
    std::filesystem::path working_directory;
};

struct WorkerExpansionResult {
    protocol::ExpansionResponse response;
    std::uint64_t request_encode_ns = 0;
    std::uint64_t transport_ns = 0;
    std::uint64_t response_decode_ns = 0;
};

class WorkerProcessError : public std::runtime_error {
  public:
    explicit WorkerProcessError(protocol::WorkerError error);

    const protocol::WorkerError& detail() const {
        return detail_;
    }

  private:
    protocol::WorkerError detail_;
};

class WorkerProcess {
  public:
    WorkerProcess() = default;
    WorkerProcess(const WorkerProcess&) = delete;
    WorkerProcess& operator=(const WorkerProcess&) = delete;
    WorkerProcess(WorkerProcess&& other) noexcept;
    WorkerProcess& operator=(WorkerProcess&& other) noexcept;
    ~WorkerProcess();

    static WorkerProcess launch(const std::filesystem::path& executable,
                                const std::vector<std::string>& arguments = {},
                                const WorkerProcessOptions& options = {});

    protocol::MacroCatalog describe();
    protocol::ExpansionResponse expand(const protocol::ExpansionRequest& request);
    WorkerExpansionResult expand_measured(const protocol::ExpansionRequest& request);
    void shutdown();

    [[nodiscard]] bool running() const;
    [[nodiscard]] int process_id() const;

  private:
    WorkerProcess(int child_pid, int write_fd, int read_fd, WorkerProcessOptions options);

    wire::Frame request(protocol::MessageKind kind, std::vector<std::uint8_t> payload = {});
    void negotiate();
    void close_descriptors();
    void terminate_noexcept();
    void stop_noexcept();

    int child_pid_ = -1;
    int write_fd_ = -1;
    int read_fd_ = -1;
    std::uint64_t next_request_id_ = 1;
    WorkerProcessOptions options_{};
};

} // namespace dudu::macro
