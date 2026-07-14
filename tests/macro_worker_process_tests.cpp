#include "dudu/macro/macro_worker_process.hpp"
#include "dudu/macro/macro_worker_runtime.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

int run_worker() {
    using namespace dudu::macro;
    using namespace dudu::macro::protocol;
    MacroCatalog catalog = {.package = "process-test",
                            .binary_identity = "process-test-v1",
                            .macros = {{.name = "Echo",
                                        .entry_point = "fixture.Echo",
                                        .accepted_kind = DeclarationKind::Class}}};
    return serve_worker(catalog, [](const ExpansionRequest& request) {
        if (request.macro_name == "fixture.Fail")
            throw std::runtime_error("fixture failed");
        if (request.macro_name == "fixture.Crash")
            _exit(70);
        if (request.macro_name == "fixture.Hang")
            std::this_thread::sleep_for(std::chrono::seconds(5));
        Expansion expansion;
        expansion.diagnostics.push_back({.severity = DiagnosticSeverity::Note,
                                         .code = "test.echo",
                                         .message = request.macro_name,
                                         .range = {},
                                         .notes = {}});
        return ExpansionResponse{.expansion = std::move(expansion), .cacheable = true};
    });
}

int run_malformed_worker() {
    const std::array<unsigned char, dudu::macro::wire::frame_header_bytes> invalid_frame{};
    const ssize_t written = ::write(STDOUT_FILENO, invalid_frame.data(), invalid_frame.size());
    return written == static_cast<ssize_t>(invalid_frame.size()) ? 0 : 1;
}

void require_echo(dudu::macro::WorkerProcess& worker) {
    dudu::macro::protocol::ExpansionRequest request = {
        .macro_name = "fixture.Echo", .declaration = {}, .invocation = {}, .compile_values = {}};
    assert(worker.expand(request).expansion.diagnostics.front().message == "fixture.Echo");
}

void test_persistent_worker(const std::filesystem::path& self) {
    using namespace dudu::macro;
    using namespace dudu::macro::protocol;
    WorkerProcess worker = WorkerProcess::launch(self, {"--worker"});
    const int child = worker.process_id();
    const MacroCatalog catalog = worker.describe();
    assert(catalog.package == "process-test");
    assert(catalog.macros.size() == 1);

    ExpansionRequest request = {
        .macro_name = "fixture.Echo", .declaration = {}, .invocation = {}, .compile_values = {}};
    const ExpansionResponse first = worker.expand(request);
    const ExpansionResponse second = worker.expand(request);
    assert(first.cacheable);
    assert(first.expansion.diagnostics.front().message == "fixture.Echo");
    assert(second.expansion.diagnostics.front().message == "fixture.Echo");
    request.macro_name = "fixture.Fail";
    bool failed = false;
    try {
        (void)worker.expand(request);
    } catch (const WorkerProcessError& error) {
        failed = true;
        assert(error.detail().code == "dudu.macro.worker");
        assert(error.detail().message == "fixture failed");
    }
    assert(failed);
    assert(worker.process_id() == child);
    worker.shutdown();
    assert(!worker.running());
}

void test_crash_retires_worker_and_allows_replacement(const std::filesystem::path& self) {
    using namespace dudu::macro;
    using namespace dudu::macro::protocol;
    WorkerProcess crashed = WorkerProcess::launch(self, {"--worker"});
    ExpansionRequest request{.macro_name = "fixture.Crash"};
    bool failed = false;
    try {
        (void)crashed.expand(request);
    } catch (const std::exception&) {
        failed = true;
    }
    assert(failed);
    assert(!crashed.running());

    WorkerProcess replacement = WorkerProcess::launch(self, {"--worker"});
    require_echo(replacement);
}

void test_timeout_retires_worker_and_allows_replacement(const std::filesystem::path& self) {
    using namespace dudu::macro;
    using namespace dudu::macro::protocol;
    WorkerProcessOptions options{.request_timeout = std::chrono::milliseconds(40),
                                 .decode_limits = {},
                                 .working_directory = {}};
    WorkerProcess hung = WorkerProcess::launch(self, {"--worker"}, options);
    ExpansionRequest request{.macro_name = "fixture.Hang"};
    const auto start = std::chrono::steady_clock::now();
    bool failed = false;
    try {
        (void)hung.expand(request);
    } catch (const std::exception& error) {
        failed = std::string(error.what()).find("timed out") != std::string::npos;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(failed);
    assert(!hung.running());
    assert(elapsed < std::chrono::milliseconds(250));

    WorkerProcess replacement = WorkerProcess::launch(self, {"--worker"}, options);
    require_echo(replacement);
}

void test_malformed_worker_is_isolated(const std::filesystem::path& self) {
    using namespace dudu::macro;
    bool failed = false;
    try {
        (void)WorkerProcess::launch(self, {"--malformed-worker"});
    } catch (const wire::ProtocolError&) {
        failed = true;
    }
    assert(failed);

    WorkerProcess replacement = WorkerProcess::launch(self, {"--worker"});
    require_echo(replacement);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--worker") {
        return run_worker();
    }
    if (argc == 2 && std::string(argv[1]) == "--malformed-worker") {
        return run_malformed_worker();
    }
    const std::filesystem::path self = std::filesystem::canonical(argv[0]);
    test_persistent_worker(self);
    test_crash_retires_worker_and_allows_replacement(self);
    test_timeout_retires_worker_and_allows_replacement(self);
    test_malformed_worker_is_isolated(self);
    return 0;
}
