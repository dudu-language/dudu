#include "dudu/project/project_index_cache.hpp"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
}

dudu::ProjectIndexOptions options_for(const std::filesystem::path& path) {
    dudu::ProjectIndexOptions options;
    options.entry_path = path;
    options.entry_source = "def answer() -> i32:\n"
                           "    return 42\n";
    options.source_dir = path.parent_path();
    options.force_module_tree = true;
    options.include_native_headers = false;
    options.check_semantics = true;
    options.semantic_options = {.check_bodies = true};
    return options;
}

void assert_answer_snapshot(const std::shared_ptr<const dudu::ProjectIndex>& snapshot) {
    assert(snapshot != nullptr);
    assert(snapshot->merged_module().functions.size() == 1);
    assert(snapshot->merged_module().functions.front().name == "answer");
}

void test_snapshot_survives_cache_clear() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_project_index_cache_lifetime_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "main.dd";
    write_file(path, "def answer() -> i32:\n"
                     "    return 42\n");

    dudu::ProjectIndexCache cache;
    const auto snapshot = cache.get_shared(options_for(path));
    cache.clear();

    assert(cache.stats().entries == 0);
    assert_answer_snapshot(snapshot);
}

void test_concurrent_cache_churn_preserves_request_snapshots() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_project_index_cache_concurrency_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "main.dd";
    write_file(path, "def answer() -> i32:\n"
                     "    return 42\n");

    dudu::ProjectIndexCache cache;
    const dudu::ProjectIndexOptions options = options_for(path);
    std::atomic<bool> start = false;
    std::vector<std::shared_ptr<const dudu::ProjectIndex>> snapshots(4);
    std::vector<std::thread> readers;
    readers.reserve(snapshots.size());
    for (size_t reader = 0; reader < snapshots.size(); ++reader) {
        readers.emplace_back([&, reader] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (size_t iteration = 0; iteration < 12; ++iteration) {
                snapshots[reader] = cache.get_shared(options);
                assert_answer_snapshot(snapshots[reader]);
                (void)cache.stats();
            }
        });
    }
    std::thread clearer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (size_t iteration = 0; iteration < 12; ++iteration) {
            cache.clear();
            std::this_thread::yield();
        }
    });

    start.store(true, std::memory_order_release);
    for (std::thread& reader : readers) {
        reader.join();
    }
    clearer.join();

    for (const auto& snapshot : snapshots) {
        assert_answer_snapshot(snapshot);
    }
}

} // namespace

int main() {
    try {
        test_snapshot_survives_cache_clear();
        test_concurrent_cache_churn_preserves_request_snapshots();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
