#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"

bench_dir="$repo_root/build/benchmarks"
mkdir -p "$bench_dir"
n="${1:-50000000}"

"$repo_root/build/duc" emit "$repo_root/benchmarks/scalar_sum.dd" -o "$bench_dir/scalar_sum_dudu.cpp"

cat >"$bench_dir/scalar_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>

int64_t scalar_sum(int32_t n);

int main(int argc, char** argv) {
    const int32_t n = argc > 1 ? int32_t(std::atoi(argv[1])) : 50000000;
    volatile int64_t result = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = scalar_sum(n);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << micros << " us result=" << result << '\n';
    return 0;
}
CPP

cxx="${CXX:-c++}"
flags=(-std=c++20 -O3 -DNDEBUG)
"$cxx" "${flags[@]}" "$bench_dir/scalar_sum_dudu.cpp" "$bench_dir/scalar_driver.cpp" \
    -o "$bench_dir/scalar_sum_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/scalar_sum.cpp" "$bench_dir/scalar_driver.cpp" \
    -o "$bench_dir/scalar_sum_cpp"

echo "dudu:"
"$bench_dir/scalar_sum_dudu" "$n"
echo "cpp:"
"$bench_dir/scalar_sum_cpp" "$n"
