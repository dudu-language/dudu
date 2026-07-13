#include "dudu/macro/macro_worker_process.hpp"
#include "dudu/macro/macro_worker_runtime.hpp"

#include <cassert>
#include <filesystem>
#include <string>

namespace {

int run_worker() {
    using namespace dudu::macro;
    using namespace dudu::macro::protocol;
    MacroCatalog catalog = {
        .package = "process-test",
        .binary_identity = "process-test-v1",
        .macros = {{.name = "Echo",
                    .entry_point = "fixture.Echo",
                    .accepted_kind = DeclarationKind::Class}}};
    return serve_worker(catalog, [](const ExpansionRequest& request) {
        Expansion expansion;
        expansion.diagnostics.push_back({.severity = DiagnosticSeverity::Note,
                                         .code = "test.echo",
                                         .message = request.macro_name,
                                         .range = {},
                                         .notes = {}});
        return ExpansionResponse{.expansion = std::move(expansion), .cacheable = true};
    });
}

void test_persistent_worker(const std::filesystem::path& self) {
    using namespace dudu::macro;
    using namespace dudu::macro::protocol;
    WorkerProcess worker = WorkerProcess::launch(self, {"--worker"});
    const int child = worker.process_id();
    const MacroCatalog catalog = worker.describe();
    assert(catalog.package == "process-test");
    assert(catalog.macros.size() == 1);

    ExpansionRequest request = {.macro_name = "fixture.Echo",
                                .declaration = {},
                                .invocation = {},
                                .compile_values = {}};
    const ExpansionResponse first = worker.expand(request);
    const ExpansionResponse second = worker.expand(request);
    assert(first.cacheable);
    assert(first.expansion.diagnostics.front().message == "fixture.Echo");
    assert(second.expansion.diagnostics.front().message == "fixture.Echo");
    assert(worker.process_id() == child);
    worker.shutdown();
    assert(!worker.running());
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--worker") {
        return run_worker();
    }
    test_persistent_worker(std::filesystem::canonical(argv[0]));
    return 0;
}
