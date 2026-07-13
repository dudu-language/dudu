#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
n="${1:-10000000}"
samples="${SAMPLES:-5}"
particle_n="${PARTICLE_N:-500000}"
particle_steps="${PARTICLE_STEPS:-20}"
thread_n="${THREAD_N:-10000000}"
thread_workers="${THREAD_WORKERS:-16}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required for the CPython comparison" >&2
    exit 1
fi
if [[ ! -x /usr/bin/time ]]; then
    echo "/usr/bin/time is required for peak RSS measurement" >&2
    exit 1
fi

"$repo_root/scripts/bench.sh" "$n" --samples "$samples"

bench_dir="$repo_root/build/benchmarks"
"$repo_root/build/duc" emit "$repo_root/benchmarks/particle_update.dd" \
    -o "$bench_dir/particle_update_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/threaded_accum.dd" \
    -o "$bench_dir/threaded_accum_dudu.cpp"
cat >"$bench_dir/particle_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

double particle_update(int32_t count, int32_t steps);

int main(int argc, char** argv) {
    const int32_t count = argc > 1 ? int32_t(std::atoi(argv[1])) : 500000;
    const int32_t steps = argc > 2 ? int32_t(std::atoi(argv[2])) : 20;
    const auto start = std::chrono::steady_clock::now();
    const volatile double result = particle_update(count, steps);
    const auto end = std::chrono::steady_clock::now();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << micros << " us result=" << result << '\n';
    return 0;
}
CPP
"${CXX:-c++}" -std=c++20 -O3 -DNDEBUG "$bench_dir/particle_update_dudu.cpp" \
    "$bench_dir/particle_driver.cpp" -o "$bench_dir/particle_update_dudu"
"${CXX:-c++}" -std=c++20 -O3 -DNDEBUG "$repo_root/benchmarks/particle_update.cpp" \
    "$bench_dir/particle_driver.cpp" -o "$bench_dir/particle_update_cpp"
cat >"$bench_dir/threaded_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

int64_t threaded_accum(int32_t worker_count, int32_t n);

int main(int argc, char** argv) {
    const int32_t workers = argc > 1 ? int32_t(std::atoi(argv[1])) : 16;
    const int32_t n = argc > 2 ? int32_t(std::atoi(argv[2])) : 10000000;
    const auto start = std::chrono::steady_clock::now();
    const volatile int64_t result = threaded_accum(workers, n);
    const auto end = std::chrono::steady_clock::now();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << micros << " us result=" << result << '\n';
    return 0;
}
CPP
"${CXX:-c++}" -std=c++20 -O3 -DNDEBUG -pthread \
    "$bench_dir/threaded_accum_dudu.cpp" "$bench_dir/threaded_driver.cpp" \
    -o "$bench_dir/threaded_accum_dudu"
"${CXX:-c++}" -std=c++20 -O3 -DNDEBUG -pthread \
    "$repo_root/benchmarks/threaded_accum.cpp" "$bench_dir/threaded_driver.cpp" \
    -o "$bench_dir/threaded_accum_cpp"

echo
echo "host"
python3 --version
if command -v lscpu >/dev/null 2>&1; then
    lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -1
fi

run_case() {
    local label="$1"
    shift
    printf '%s\n' "$label"
    /usr/bin/time -f 'wall=%e s maxrss=%M KiB' "$@"
}

echo
run_case "CPython scalar" \
    python3 "$repo_root/benchmarks/python_compare.py" scalar "$n"
run_case "Dudu scalar" "$repo_root/build/benchmarks/scalar_sum_dudu" "$n"
run_case "C++ scalar" "$repo_root/build/benchmarks/scalar_sum_cpp" "$n"
run_case "CPython list" \
    python3 "$repo_root/benchmarks/python_compare.py" list "$n"
run_case "Dudu list" "$repo_root/build/benchmarks/list_accum_dudu" "$n"
run_case "C++ list" "$repo_root/build/benchmarks/list_accum_cpp" "$n"
run_case "CPython particle array" \
    python3 "$repo_root/benchmarks/python_compare.py" particle "$particle_n" \
        --steps "$particle_steps" --calls 1
run_case "Dudu particle array" "$repo_root/build/benchmarks/particle_update_dudu" \
    "$particle_n" "$particle_steps"
run_case "C++ particle array" "$repo_root/build/benchmarks/particle_update_cpp" \
    "$particle_n" "$particle_steps"
run_case "CPython threaded CPU work" \
    python3 "$repo_root/benchmarks/python_compare.py" threaded "$thread_n" \
        --workers "$thread_workers" --calls 1
run_case "Dudu threaded CPU work" "$repo_root/build/benchmarks/threaded_accum_dudu" \
    "$thread_workers" "$thread_n"
run_case "C++ threaded CPU work" "$repo_root/build/benchmarks/threaded_accum_cpp" \
    "$thread_workers" "$thread_n"
