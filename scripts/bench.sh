#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"

bench_dir="$repo_root/build/benchmarks"
mkdir -p "$bench_dir"
n=50000000
report_path=""
max_ratio=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --emit-report)
            report_path="${2:?--emit-report requires a path}"
            shift 2
            ;;
        --max-ratio)
            max_ratio="${2:?--max-ratio requires a value}"
            shift 2
            ;;
        *)
            n="$1"
            shift
            ;;
    esac
done

"$repo_root/build/duc" emit "$repo_root/benchmarks/scalar_sum.dd" -o "$bench_dir/scalar_sum_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/pointer_sum.dd" -o "$bench_dir/pointer_sum_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/field_dot.dd" -o "$bench_dir/field_dot_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/tuple_accum.dd" -o "$bench_dir/tuple_accum_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/callback_accum.dd" \
    -o "$bench_dir/callback_accum_dudu.cpp"

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

cat >"$bench_dir/pointer_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

int64_t pointer_sum(const int32_t* data, int32_t n);

int main(int argc, char** argv) {
    const int32_t n = argc > 1 ? int32_t(std::atoi(argv[1])) : 50000000;
    std::vector<int32_t> data(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        data[static_cast<size_t>(i)] = i & 1023;
    }
    volatile int64_t result = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = pointer_sum(data.data(), n);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << micros << " us result=" << result << '\n';
    return 0;
}
CPP

cat >"$bench_dir/field_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

struct Vec2 {
    float x{};
    float y{};
};

float field_dot(Vec2 a, Vec2 b, int32_t n);

int main(int argc, char** argv) {
    const int32_t n = argc > 1 ? int32_t(std::atoi(argv[1])) : 50000000;
    volatile float result = 0.0F;
    const Vec2 a{1.25F, 2.5F};
    const Vec2 b{3.0F, 4.0F};
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = field_dot(a, b, n);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << micros << " us result=" << result << '\n';
    return 0;
}
CPP

cat >"$bench_dir/tuple_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

int64_t tuple_accum(int32_t n);

int main(int argc, char** argv) {
    const int32_t n = argc > 1 ? int32_t(std::atoi(argv[1])) : 50000000;
    volatile int64_t result = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = tuple_accum(n);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << micros << " us result=" << result << '\n';
    return 0;
}
CPP

cat >"$bench_dir/callback_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

int64_t callback_accum(int32_t n);

int main(int argc, char** argv) {
    const int32_t n = argc > 1 ? int32_t(std::atoi(argv[1])) : 50000000;
    volatile int64_t result = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = callback_accum(n);
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
"$cxx" "${flags[@]}" "$bench_dir/pointer_sum_dudu.cpp" "$bench_dir/pointer_driver.cpp" \
    -o "$bench_dir/pointer_sum_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/pointer_sum.cpp" "$bench_dir/pointer_driver.cpp" \
    -o "$bench_dir/pointer_sum_cpp"
"$cxx" "${flags[@]}" "$bench_dir/field_dot_dudu.cpp" "$bench_dir/field_driver.cpp" \
    -o "$bench_dir/field_dot_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/field_dot.cpp" "$bench_dir/field_driver.cpp" \
    -o "$bench_dir/field_dot_cpp"
"$cxx" "${flags[@]}" "$bench_dir/tuple_accum_dudu.cpp" "$bench_dir/tuple_driver.cpp" \
    -o "$bench_dir/tuple_accum_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/tuple_accum.cpp" "$bench_dir/tuple_driver.cpp" \
    -o "$bench_dir/tuple_accum_cpp"
"$cxx" "${flags[@]}" "$bench_dir/callback_accum_dudu.cpp" "$bench_dir/callback_driver.cpp" \
    -o "$bench_dir/callback_accum_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/callback_accum.cpp" "$bench_dir/callback_driver.cpp" \
    -o "$bench_dir/callback_accum_cpp"

run_bench() {
    local label="$1"
    local bin="$2"
    echo "$label:"
    local output
    output="$("$bin" "$n")"
    echo "$output"
    printf '%s|%s|%s\n' "$label" "${output%% us*}" "${output##*result=}" >>"$bench_dir/results.tsv"
}

rm -f "$bench_dir/results.tsv"
run_bench "scalar dudu" "$bench_dir/scalar_sum_dudu"
run_bench "scalar cpp" "$bench_dir/scalar_sum_cpp"
run_bench "pointer dudu" "$bench_dir/pointer_sum_dudu"
run_bench "pointer cpp" "$bench_dir/pointer_sum_cpp"
run_bench "field dudu" "$bench_dir/field_dot_dudu"
run_bench "field cpp" "$bench_dir/field_dot_cpp"
run_bench "tuple dudu" "$bench_dir/tuple_accum_dudu"
run_bench "tuple cpp" "$bench_dir/tuple_accum_cpp"
run_bench "callback dudu" "$bench_dir/callback_accum_dudu"
run_bench "callback cpp" "$bench_dir/callback_accum_cpp"

bench_micros() {
    local label="$1"
    awk -F'|' -v label="$label" '$1 == label { print $2 }' "$bench_dir/results.tsv"
}

bench_ratio() {
    local dudu="$1"
    local cpp="$2"
    awk -v dudu="$dudu" -v cpp="$cpp" 'BEGIN {
        if (cpp == 0) {
            print "null"
        } else {
            printf "%.6f", dudu / cpp
        }
    }'
}

check_ratio() {
    local name="$1"
    local ratio="$2"
    if [[ -z "$max_ratio" || "$ratio" == "null" ]]; then
        return
    fi
    awk -v name="$name" -v ratio="$ratio" -v max="$max_ratio" 'BEGIN {
        if (ratio > max) {
            printf "%s ratio %.6f exceeds %.6f\n", name, ratio, max > "/dev/stderr"
            exit 1
        }
    }'
}

scalar_dudu_micros="$(bench_micros "scalar dudu")"
scalar_cpp_micros="$(bench_micros "scalar cpp")"
pointer_dudu_micros="$(bench_micros "pointer dudu")"
pointer_cpp_micros="$(bench_micros "pointer cpp")"
field_dudu_micros="$(bench_micros "field dudu")"
field_cpp_micros="$(bench_micros "field cpp")"
tuple_dudu_micros="$(bench_micros "tuple dudu")"
tuple_cpp_micros="$(bench_micros "tuple cpp")"
callback_dudu_micros="$(bench_micros "callback dudu")"
callback_cpp_micros="$(bench_micros "callback cpp")"
scalar_ratio="$(bench_ratio "$scalar_dudu_micros" "$scalar_cpp_micros")"
pointer_ratio="$(bench_ratio "$pointer_dudu_micros" "$pointer_cpp_micros")"
field_ratio="$(bench_ratio "$field_dudu_micros" "$field_cpp_micros")"
tuple_ratio="$(bench_ratio "$tuple_dudu_micros" "$tuple_cpp_micros")"
callback_ratio="$(bench_ratio "$callback_dudu_micros" "$callback_cpp_micros")"
check_ratio scalar "$scalar_ratio"
check_ratio pointer "$pointer_ratio"
check_ratio field "$field_ratio"
check_ratio tuple "$tuple_ratio"
check_ratio callback "$callback_ratio"

if [[ -n "$report_path" ]]; then
    mkdir -p "$(dirname "$report_path")"
    {
        printf '{\n'
        printf '  "compiler": "%s",\n' "$("$repo_root/build/duc" --version)"
        printf '  "cxx": "%s",\n' "$cxx"
        printf '  "flags": "%s",\n' "${flags[*]}"
        printf '  "n": %s,\n' "$n"
        printf '  "benchmarks": [\n'
        first=1
        while IFS='|' read -r label micros result; do
            [[ "$first" -eq 0 ]] && printf ',\n'
            first=0
            printf '    {"name": "%s", "micros": %s, "result": "%s"}' "$label" "$micros" "$result"
        done <"$bench_dir/results.tsv"
        printf '\n  ],\n'
        printf '  "comparisons": [\n'
        printf '    {"name": "scalar", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$scalar_dudu_micros" "$scalar_cpp_micros" "$scalar_ratio"
        printf '    {"name": "pointer", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$pointer_dudu_micros" "$pointer_cpp_micros" "$pointer_ratio"
        printf '    {"name": "field", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$field_dudu_micros" "$field_cpp_micros" "$field_ratio"
        printf '    {"name": "tuple", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$tuple_dudu_micros" "$tuple_cpp_micros" "$tuple_ratio"
        printf '    {"name": "callback", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s}\n' \
            "$callback_dudu_micros" "$callback_cpp_micros" "$callback_ratio"
        printf '  ],\n'
        printf '  "generated": ["%s", "%s", "%s", "%s", "%s"],\n' \
            "$bench_dir/scalar_sum_dudu.cpp" "$bench_dir/pointer_sum_dudu.cpp" \
            "$bench_dir/field_dot_dudu.cpp" "$bench_dir/tuple_accum_dudu.cpp" \
            "$bench_dir/callback_accum_dudu.cpp"
        printf '  "comparison_sources": ["%s", "%s", "%s", "%s", "%s"]\n' \
            "$repo_root/benchmarks/scalar_sum.cpp" "$repo_root/benchmarks/pointer_sum.cpp" \
            "$repo_root/benchmarks/field_dot.cpp" "$repo_root/benchmarks/tuple_accum.cpp" \
            "$repo_root/benchmarks/callback_accum.cpp"
        printf '}\n'
    } >"$report_path"
fi
