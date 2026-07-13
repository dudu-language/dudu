#include <cstdint>
#include <thread>
#include <vector>

namespace {
void accumulate_worker(int32_t index, int32_t n, std::vector<int64_t>& output) {
    int64_t total = 0;
    for (int32_t i = 0; i < n; ++i) {
        total += int64_t(i) * 3;
    }
    output[static_cast<size_t>(index)] = total;
}
} // namespace

int64_t threaded_accum(int32_t worker_count, int32_t n) {
    std::vector<int64_t> output(static_cast<size_t>(worker_count));
    std::vector<std::thread> threads;

    for (int32_t worker_index = 0; worker_index < worker_count; ++worker_index) {
        threads.emplace_back(accumulate_worker, worker_index, n, std::ref(output));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    int64_t total = 0;
    for (int64_t value : output) {
        total += value;
    }
    return total;
}
