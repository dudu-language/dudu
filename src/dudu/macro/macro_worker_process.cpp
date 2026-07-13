#include "dudu/macro/macro_worker_process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <span>
#include <stdexcept>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace dudu::macro {
namespace {

using Clock = std::chrono::steady_clock;

std::runtime_error system_error(std::string_view operation) {
    return std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

int remaining_timeout_ms(Clock::time_point deadline) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (remaining <= 0) {
        return 0;
    }
    return static_cast<int>(std::min<std::int64_t>(remaining, 2'147'483'647));
}

void wait_ready(int fd, short events, Clock::time_point deadline) {
    pollfd descriptor{.fd = fd, .events = events, .revents = 0};
    while (true) {
        const int result = ::poll(&descriptor, 1, remaining_timeout_ms(deadline));
        if (result > 0) {
            if ((descriptor.revents & events) != 0) {
                return;
            }
            throw std::runtime_error("macro worker pipe closed unexpectedly");
        }
        if (result == 0) {
            throw std::runtime_error("macro worker request timed out");
        }
        if (errno != EINTR) {
            throw system_error("could not poll macro worker pipe");
        }
    }
}

void write_exact(int fd, std::span<const std::uint8_t> bytes, Clock::time_point deadline) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        wait_ready(fd, POLLOUT, deadline);
        const ssize_t count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno != EINTR) {
            throw system_error("could not write macro worker request");
        }
    }
}

void read_exact(int fd, std::span<std::uint8_t> bytes, Clock::time_point deadline) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        wait_ready(fd, POLLIN, deadline);
        const ssize_t count = ::read(fd, bytes.data() + offset, bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count == 0) {
            throw std::runtime_error("macro worker exited before replying");
        } else if (errno != EINTR) {
            throw system_error("could not read macro worker response");
        }
    }
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

wire::Frame read_frame(int fd, Clock::time_point deadline, const wire::DecodeLimits& limits) {
    std::array<std::uint8_t, wire::frame_header_bytes> header{};
    read_exact(fd, header, deadline);
    const std::uint32_t payload_size = read_u32(header, 16);
    if (payload_size > limits.max_frame_bytes) {
        throw wire::ProtocolError("macro worker frame exceeds size limit");
    }
    std::vector<std::uint8_t> bytes(header.begin(), header.end());
    bytes.resize(header.size() + payload_size);
    read_exact(fd, std::span(bytes).subspan(header.size()), deadline);
    return wire::decode_frame(bytes, limits);
}

std::string worker_error_message(const wire::Frame& response,
                                 const wire::DecodeLimits& limits) {
    const protocol::WorkerError error = protocol::decode_WorkerError(response.payload, limits);
    return error.code + ": " + error.message;
}

} // namespace

WorkerProcess::WorkerProcess(int child_pid, int write_fd, int read_fd,
                             WorkerProcessOptions options)
    : child_pid_(child_pid), write_fd_(write_fd), read_fd_(read_fd), options_(options) {}

WorkerProcess::WorkerProcess(WorkerProcess&& other) noexcept {
    *this = std::move(other);
}

WorkerProcess& WorkerProcess::operator=(WorkerProcess&& other) noexcept {
    if (this != &other) {
        stop_noexcept();
        child_pid_ = std::exchange(other.child_pid_, -1);
        write_fd_ = std::exchange(other.write_fd_, -1);
        read_fd_ = std::exchange(other.read_fd_, -1);
        next_request_id_ = other.next_request_id_;
        options_ = other.options_;
    }
    return *this;
}

WorkerProcess::~WorkerProcess() {
    stop_noexcept();
}

WorkerProcess WorkerProcess::launch(const std::filesystem::path& executable,
                                    const std::vector<std::string>& arguments,
                                    const WorkerProcessOptions& options) {
    int request_pipe[2]{};
    int response_pipe[2]{};
    if (::pipe(request_pipe) != 0) {
        throw system_error("could not create macro worker request pipe");
    }
    if (::pipe(response_pipe) != 0) {
        ::close(request_pipe[0]);
        ::close(request_pipe[1]);
        throw system_error("could not create macro worker response pipe");
    }
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(request_pipe[0]);
        ::close(request_pipe[1]);
        ::close(response_pipe[0]);
        ::close(response_pipe[1]);
        throw system_error("could not fork macro worker");
    }
    if (child == 0) {
        ::dup2(request_pipe[0], STDIN_FILENO);
        ::dup2(response_pipe[1], STDOUT_FILENO);
        ::close(request_pipe[0]);
        ::close(request_pipe[1]);
        ::close(response_pipe[0]);
        ::close(response_pipe[1]);
        std::vector<std::string> storage;
        storage.reserve(arguments.size() + 1);
        storage.push_back(executable.string());
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1);
        for (std::string& argument : storage) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);
        ::execv(argv.front(), argv.data());
        _exit(127);
    }
    ::close(request_pipe[0]);
    ::close(response_pipe[1]);
    WorkerProcess process(static_cast<int>(child), request_pipe[1], response_pipe[0], options);
    try {
        process.negotiate();
    } catch (...) {
        process.stop_noexcept();
        throw;
    }
    return process;
}

wire::Frame WorkerProcess::request(protocol::MessageKind kind,
                                   std::vector<std::uint8_t> payload) {
    if (!running()) {
        throw std::runtime_error("macro worker is not running");
    }
    const std::uint64_t request_id = next_request_id_++;
    const wire::Frame frame = {
        .protocol_version = static_cast<std::uint16_t>(protocol::protocol_version),
        .message_kind = static_cast<std::uint16_t>(kind),
        .request_id = request_id,
        .payload = std::move(payload)};
    const Clock::time_point deadline = Clock::now() + options_.request_timeout;
    write_exact(write_fd_, wire::encode_frame(frame), deadline);
    wire::Frame response = read_frame(read_fd_, deadline, options_.decode_limits);
    if (response.request_id != request_id) {
        throw wire::ProtocolError("macro worker response request ID mismatch");
    }
    if (response.protocol_version != protocol::protocol_version) {
        throw wire::ProtocolError("macro worker response protocol version mismatch");
    }
    if (static_cast<protocol::MessageKind>(response.message_kind) ==
        protocol::MessageKind::WorkerError) {
        throw std::runtime_error(worker_error_message(response, options_.decode_limits));
    }
    return response;
}

void WorkerProcess::negotiate() {
    const protocol::Hello hello = {.compiler_version = "dudu",
                                   .protocol_version = protocol::protocol_version,
                                   .schema_version = protocol::schema_version};
    const wire::Frame response = request(protocol::MessageKind::Hello, protocol::encode(hello));
    if (static_cast<protocol::MessageKind>(response.message_kind) !=
        protocol::MessageKind::HelloAck) {
        throw wire::ProtocolError("macro worker did not reply with HelloAck");
    }
    const protocol::HelloAck ack =
        protocol::decode_HelloAck(response.payload, options_.decode_limits);
    if (!ack.accepted) {
        throw wire::ProtocolError("macro worker rejected protocol negotiation: " + ack.reason);
    }
}

protocol::MacroCatalog WorkerProcess::describe() {
    const wire::Frame response = request(protocol::MessageKind::Describe);
    if (static_cast<protocol::MessageKind>(response.message_kind) !=
        protocol::MessageKind::Catalog) {
        throw wire::ProtocolError("macro worker did not reply with Catalog");
    }
    return protocol::decode_MacroCatalog(response.payload, options_.decode_limits);
}

protocol::ExpansionResponse WorkerProcess::expand(
    const protocol::ExpansionRequest& expansion_request) {
    const wire::Frame response =
        request(protocol::MessageKind::Expand, protocol::encode(expansion_request));
    if (static_cast<protocol::MessageKind>(response.message_kind) !=
        protocol::MessageKind::ExpansionResult) {
        throw wire::ProtocolError("macro worker did not reply with ExpansionResult");
    }
    return protocol::decode_ExpansionResponse(response.payload, options_.decode_limits);
}

void WorkerProcess::shutdown() {
    if (!running()) {
        return;
    }
    const std::uint64_t request_id = next_request_id_++;
    const wire::Frame frame = {
        .protocol_version = static_cast<std::uint16_t>(protocol::protocol_version),
        .message_kind = static_cast<std::uint16_t>(protocol::MessageKind::Shutdown),
        .request_id = request_id,
        .payload = {}};
    const Clock::time_point deadline = Clock::now() + options_.request_timeout;
    write_exact(write_fd_, wire::encode_frame(frame), deadline);
    ::close(write_fd_);
    write_fd_ = -1;
    int status = 0;
    while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {
    }
    child_pid_ = -1;
    close_descriptors();
}

bool WorkerProcess::running() const {
    return child_pid_ >= 0;
}

int WorkerProcess::process_id() const {
    return child_pid_;
}

void WorkerProcess::close_descriptors() {
    if (write_fd_ >= 0) {
        ::close(write_fd_);
        write_fd_ = -1;
    }
    if (read_fd_ >= 0) {
        ::close(read_fd_);
        read_fd_ = -1;
    }
}

void WorkerProcess::stop_noexcept() {
    if (child_pid_ < 0) {
        close_descriptors();
        return;
    }
    try {
        shutdown();
        return;
    } catch (...) {
    }
    ::kill(child_pid_, SIGKILL);
    int status = 0;
    while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {
    }
    child_pid_ = -1;
    close_descriptors();
}

} // namespace dudu::macro
