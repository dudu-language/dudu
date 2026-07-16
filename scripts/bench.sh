#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"

bench_dir="$repo_root/build/benchmarks"
mkdir -p "$bench_dir"
n=50000000
report_path=""
max_ratio=""
samples=1
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
        --samples)
            samples="${2:?--samples requires a count}"
            shift 2
            ;;
        *)
            n="$1"
            shift
            ;;
    esac
done
if [[ "$samples" -lt 1 ]]; then
    echo "--samples must be at least 1" >&2
    exit 1
fi

"$repo_root/build/duc" emit "$repo_root/benchmarks/scalar_sum.dd" -o "$bench_dir/scalar_sum_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/pointer_sum.dd" -o "$bench_dir/pointer_sum_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/field_dot.dd" -o "$bench_dir/field_dot_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/fixed_array_mix.dd" \
    -o "$bench_dir/fixed_array_mix_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/list_accum.dd" -o "$bench_dir/list_accum_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/tuple_accum.dd" -o "$bench_dir/tuple_accum_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/callback_accum.dd" \
    -o "$bench_dir/callback_accum_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/particle_update.dd" \
    -o "$bench_dir/particle_update_dudu.cpp"
"$repo_root/build/duc" emit "$repo_root/benchmarks/threaded_accum.dd" \
    -o "$bench_dir/threaded_accum_dudu.cpp"

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

cat >"$bench_dir/fixed_array_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

int64_t fixed_array_mix(int32_t n);

int main(int argc, char** argv) {
    const int32_t n = argc > 1 ? int32_t(std::atoi(argv[1])) : 50000000;
    volatile int64_t result = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = fixed_array_mix(n);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << micros << " us result=" << result << '\n';
    return 0;
}
CPP

cat >"$bench_dir/list_driver.cpp" <<'CPP'
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

int64_t list_accum(int32_t n);

int main(int argc, char** argv) {
    const int32_t n = argc > 1 ? int32_t(std::atoi(argv[1])) : 5000000;
    volatile int64_t result = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = list_accum(n);
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

cat >"$bench_dir/particle_driver.cpp" <<'CPP'
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

double particle_update(int32_t count, int32_t steps);

int main(int argc, char** argv) {
    const int32_t operations = argc > 1 ? int32_t(std::atoi(argv[1])) : 10000000;
    const int32_t steps = 10;
    const int32_t count = std::max<int32_t>(1, operations / steps);
    volatile double result = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = particle_update(count, steps);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << micros << " us result=" << result << '\n';
    return 0;
}
CPP

cat >"$bench_dir/threaded_driver.cpp" <<'CPP'
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

int64_t threaded_accum(int32_t worker_count, int32_t n);

int main(int argc, char** argv) {
    const int32_t operations = argc > 1 ? int32_t(std::atoi(argv[1])) : 10000000;
    const int32_t workers = 8;
    const int32_t per_worker = std::max<int32_t>(1, operations / workers);
    volatile int64_t result = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        result = threaded_accum(workers, per_worker);
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
"$cxx" "${flags[@]}" "$bench_dir/fixed_array_mix_dudu.cpp" "$bench_dir/fixed_array_driver.cpp" \
    -o "$bench_dir/fixed_array_mix_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/fixed_array_mix.cpp" \
    "$bench_dir/fixed_array_driver.cpp" -o "$bench_dir/fixed_array_mix_cpp"
"$cxx" "${flags[@]}" "$bench_dir/list_accum_dudu.cpp" "$bench_dir/list_driver.cpp" \
    -o "$bench_dir/list_accum_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/list_accum.cpp" "$bench_dir/list_driver.cpp" \
    -o "$bench_dir/list_accum_cpp"
"$cxx" "${flags[@]}" "$bench_dir/tuple_accum_dudu.cpp" "$bench_dir/tuple_driver.cpp" \
    -o "$bench_dir/tuple_accum_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/tuple_accum.cpp" "$bench_dir/tuple_driver.cpp" \
    -o "$bench_dir/tuple_accum_cpp"
"$cxx" "${flags[@]}" "$bench_dir/callback_accum_dudu.cpp" "$bench_dir/callback_driver.cpp" \
    -o "$bench_dir/callback_accum_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/callback_accum.cpp" "$bench_dir/callback_driver.cpp" \
    -o "$bench_dir/callback_accum_cpp"
"$cxx" "${flags[@]}" "$bench_dir/particle_update_dudu.cpp" \
    "$bench_dir/particle_driver.cpp" -o "$bench_dir/particle_update_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/particle_update.cpp" \
    "$bench_dir/particle_driver.cpp" -o "$bench_dir/particle_update_cpp"
"$cxx" "${flags[@]}" "$bench_dir/threaded_accum_dudu.cpp" \
    "$bench_dir/threaded_driver.cpp" -pthread -o "$bench_dir/threaded_accum_dudu"
"$cxx" "${flags[@]}" "$repo_root/benchmarks/threaded_accum.cpp" \
    "$bench_dir/threaded_driver.cpp" -pthread -o "$bench_dir/threaded_accum_cpp"

run_bench() {
    local label="$1"
    local bin="$2"
    echo "$label:"
    local output
    for ((sample = 1; sample <= samples; ++sample)); do
        output="$("$bin" "$n")"
        if [[ "$samples" -eq 1 ]]; then
            echo "$output"
        else
            echo "sample $sample/$samples: $output"
        fi
        printf '%s|%s|%s|%s\n' "$label" "$sample" "${output%% us*}" \
            "${output##*result=}" >>"$bench_dir/results.tsv"
    done
}

rm -f "$bench_dir/results.tsv"
run_bench "scalar dudu" "$bench_dir/scalar_sum_dudu"
run_bench "scalar cpp" "$bench_dir/scalar_sum_cpp"
run_bench "pointer dudu" "$bench_dir/pointer_sum_dudu"
run_bench "pointer cpp" "$bench_dir/pointer_sum_cpp"
run_bench "field dudu" "$bench_dir/field_dot_dudu"
run_bench "field cpp" "$bench_dir/field_dot_cpp"
run_bench "fixed_array dudu" "$bench_dir/fixed_array_mix_dudu"
run_bench "fixed_array cpp" "$bench_dir/fixed_array_mix_cpp"
run_bench "list dudu" "$bench_dir/list_accum_dudu"
run_bench "list cpp" "$bench_dir/list_accum_cpp"
run_bench "tuple dudu" "$bench_dir/tuple_accum_dudu"
run_bench "tuple cpp" "$bench_dir/tuple_accum_cpp"
run_bench "callback dudu" "$bench_dir/callback_accum_dudu"
run_bench "callback cpp" "$bench_dir/callback_accum_cpp"
run_bench "particle dudu" "$bench_dir/particle_update_dudu"
run_bench "particle cpp" "$bench_dir/particle_update_cpp"
run_bench "threaded dudu" "$bench_dir/threaded_accum_dudu"
run_bench "threaded cpp" "$bench_dir/threaded_accum_cpp"

summarize_label() {
    local label="$1"
    mapfile -t values < <(awk -F'|' -v label="$label" '$1 == label { print $3 }' \
        "$bench_dir/results.tsv" | sort -n)
    local count="${#values[@]}"
    if [[ "$count" -eq 0 ]]; then
        echo "missing benchmark results for $label" >&2
        exit 1
    fi
    local result
    result="$(awk -F'|' -v label="$label" '$1 == label { print $4; exit }' \
        "$bench_dir/results.tsv")"
    local sum=0
    for value in "${values[@]}"; do
        sum=$((sum + value))
    done
    local mean
    mean="$(awk -v sum="$sum" -v count="$count" 'BEGIN { printf "%.3f", sum / count }')"
    local median="${values[$((count / 2))]}"
    local p95_index=$(((count * 95 + 99) / 100 - 1))
    if [[ "$p95_index" -ge "$count" ]]; then
        p95_index=$((count - 1))
    fi
    printf '%s|%s|%s|%s|%s|%s\n' "$label" "$mean" "$median" \
        "${values[$p95_index]}" "$count" "$result" >>"$bench_dir/summary.tsv"
}

rm -f "$bench_dir/summary.tsv"
summarize_label "scalar dudu"
summarize_label "scalar cpp"
summarize_label "pointer dudu"
summarize_label "pointer cpp"
summarize_label "field dudu"
summarize_label "field cpp"
summarize_label "fixed_array dudu"
summarize_label "fixed_array cpp"
summarize_label "list dudu"
summarize_label "list cpp"
summarize_label "tuple dudu"
summarize_label "tuple cpp"
summarize_label "callback dudu"
summarize_label "callback cpp"
summarize_label "particle dudu"
summarize_label "particle cpp"
summarize_label "threaded dudu"
summarize_label "threaded cpp"

bench_micros() {
    local label="$1"
    awk -F'|' -v label="$label" '$1 == label { print $3 }' "$bench_dir/summary.tsv"
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
fixed_array_dudu_micros="$(bench_micros "fixed_array dudu")"
fixed_array_cpp_micros="$(bench_micros "fixed_array cpp")"
list_dudu_micros="$(bench_micros "list dudu")"
list_cpp_micros="$(bench_micros "list cpp")"
tuple_dudu_micros="$(bench_micros "tuple dudu")"
tuple_cpp_micros="$(bench_micros "tuple cpp")"
callback_dudu_micros="$(bench_micros "callback dudu")"
callback_cpp_micros="$(bench_micros "callback cpp")"
particle_dudu_micros="$(bench_micros "particle dudu")"
particle_cpp_micros="$(bench_micros "particle cpp")"
threaded_dudu_micros="$(bench_micros "threaded dudu")"
threaded_cpp_micros="$(bench_micros "threaded cpp")"
scalar_ratio="$(bench_ratio "$scalar_dudu_micros" "$scalar_cpp_micros")"
pointer_ratio="$(bench_ratio "$pointer_dudu_micros" "$pointer_cpp_micros")"
field_ratio="$(bench_ratio "$field_dudu_micros" "$field_cpp_micros")"
fixed_array_ratio="$(bench_ratio "$fixed_array_dudu_micros" "$fixed_array_cpp_micros")"
list_ratio="$(bench_ratio "$list_dudu_micros" "$list_cpp_micros")"
tuple_ratio="$(bench_ratio "$tuple_dudu_micros" "$tuple_cpp_micros")"
callback_ratio="$(bench_ratio "$callback_dudu_micros" "$callback_cpp_micros")"
particle_ratio="$(bench_ratio "$particle_dudu_micros" "$particle_cpp_micros")"
threaded_ratio="$(bench_ratio "$threaded_dudu_micros" "$threaded_cpp_micros")"
check_ratio scalar "$scalar_ratio"
check_ratio pointer "$pointer_ratio"
check_ratio field "$field_ratio"
check_ratio fixed_array "$fixed_array_ratio"
check_ratio list "$list_ratio"
check_ratio tuple "$tuple_ratio"
check_ratio callback "$callback_ratio"
check_ratio particle "$particle_ratio"
check_ratio threaded "$threaded_ratio"

if [[ -n "$report_path" ]]; then
    mkdir -p "$(dirname "$report_path")"
    {
        printf '{\n'
        printf '  "compiler": "%s",\n' "$("$repo_root/build/duc" --version)"
        printf '  "cxx": "%s",\n' "$cxx"
        printf '  "flags": "%s",\n' "${flags[*]}"
        printf '  "n": %s,\n' "$n"
        printf '  "samples": %s,\n' "$samples"
        printf '  "benchmarks": [\n'
        first=1
        while IFS='|' read -r label mean median p95 count result; do
            [[ "$first" -eq 0 ]] && printf ',\n'
            first=0
            printf '    {"name": "%s", "mean_micros": %s, ' "$label" "$mean"
            printf '"median_micros": %s, "p95_micros": %s, ' "$median" "$p95"
            printf '"samples": %s, "result": "%s"}' "$count" "$result"
        done <"$bench_dir/summary.tsv"
        printf '\n  ],\n'
        printf '  "comparisons": [\n'
        printf '    {"name": "scalar", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$scalar_dudu_micros" "$scalar_cpp_micros" "$scalar_ratio"
        printf '    {"name": "pointer", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$pointer_dudu_micros" "$pointer_cpp_micros" "$pointer_ratio"
        printf '    {"name": "field", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$field_dudu_micros" "$field_cpp_micros" "$field_ratio"
        printf '    {"name": "fixed_array", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$fixed_array_dudu_micros" "$fixed_array_cpp_micros" "$fixed_array_ratio"
        printf '    {"name": "list", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$list_dudu_micros" "$list_cpp_micros" "$list_ratio"
        printf '    {"name": "tuple", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$tuple_dudu_micros" "$tuple_cpp_micros" "$tuple_ratio"
        printf '    {"name": "callback", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$callback_dudu_micros" "$callback_cpp_micros" "$callback_ratio"
        printf '    {"name": "particle", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s},\n' \
            "$particle_dudu_micros" "$particle_cpp_micros" "$particle_ratio"
        printf '    {"name": "threaded", "dudu_micros": %s, "cpp_micros": %s, "ratio": %s}\n' \
            "$threaded_dudu_micros" "$threaded_cpp_micros" "$threaded_ratio"
        printf '  ],\n'
        printf '  "generated": ["%s", "%s", "%s", "%s", "%s", "%s", "%s", "%s", "%s"],\n' \
            "$bench_dir/scalar_sum_dudu.cpp" "$bench_dir/pointer_sum_dudu.cpp" \
            "$bench_dir/field_dot_dudu.cpp" "$bench_dir/fixed_array_mix_dudu.cpp" \
            "$bench_dir/list_accum_dudu.cpp" "$bench_dir/tuple_accum_dudu.cpp" \
            "$bench_dir/callback_accum_dudu.cpp" "$bench_dir/particle_update_dudu.cpp" \
            "$bench_dir/threaded_accum_dudu.cpp"
        printf '  "comparison_sources": ["%s", "%s", "%s", "%s", "%s", "%s", "%s", "%s", "%s"]\n' \
            "$repo_root/benchmarks/scalar_sum.cpp" "$repo_root/benchmarks/pointer_sum.cpp" \
            "$repo_root/benchmarks/field_dot.cpp" "$repo_root/benchmarks/fixed_array_mix.cpp" \
            "$repo_root/benchmarks/list_accum.cpp" "$repo_root/benchmarks/tuple_accum.cpp" \
            "$repo_root/benchmarks/callback_accum.cpp" "$repo_root/benchmarks/particle_update.cpp" \
            "$repo_root/benchmarks/threaded_accum.cpp"
        printf '}\n'
    } >"$report_path"
fi
