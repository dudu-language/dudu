#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
samples=3
csv_path="$repo_root/build/bench_compiler/compiler_bench.csv"
build_tools=1
build_type="Debug"
line_scales="1000,5000,10000"
shapes="functions,classes,expressions,modules,calls,control,arrays,indexing,generics,matches,operators,native,mixed"

usage() {
    cat <<'EOF'
usage: scripts/bench_compiler.sh [--samples N] [--csv path] [--build-type Debug|Release] [--line-scales list] [--shapes list] [--no-build]

Measures Dudu compiler and project-driver latency. This is an explicit
developer benchmark, not part of the fast correctness loop.

--line-scales accepts a comma-separated list of approximate generated Dudu
source line counts, for example:
  --line-scales 10000,50000,100000,200000,500000,1000000

--shapes accepts a comma-separated list of generated frontend stress shapes:
  functions,classes,expressions,modules,calls,control,arrays,indexing,generics,matches,operators,native,stdlib,mixed

The stdlib shape is intentionally not in the default set because real C++
standard-library header scanning is much slower than pure Dudu frontend
shapes. The native shape uses small local fixture headers and stays in the
default set to catch native interop frontend regressions without turning every
run into a libstdc++ scan benchmark.

--build-type chooses the compiler binary build used for the benchmark. Debug
matches the normal dev loop. Release measures shipped-tool speed.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --samples)
            samples="${2:?--samples requires a count}"
            shift 2
            ;;
        --csv)
            csv_path="${2:?--csv requires a path}"
            shift 2
            ;;
        --build-type)
            build_type="${2:?--build-type requires Debug or Release}"
            shift 2
            ;;
        --line-scales)
            line_scales="${2:?--line-scales requires a comma-separated list}"
            shift 2
            ;;
        --shapes)
            shapes="${2:?--shapes requires a comma-separated list}"
            shift 2
            ;;
        --no-build)
            build_tools=0
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ "$samples" -lt 1 ]]; then
    echo "--samples must be at least 1" >&2
    exit 1
fi

case "$build_type" in
    Debug|Release|RelWithDebInfo)
        ;;
    *)
        echo "--build-type must be Debug, Release, or RelWithDebInfo" >&2
        exit 1
        ;;
esac

tool_build_dir="$repo_root/build"
if [[ "$build_type" != "Debug" ]]; then
    tool_build_dir="$repo_root/build-${build_type,,}"
fi

if [[ "$build_tools" -eq 1 ]]; then
    cmake -S "$repo_root" -B "$tool_build_dir" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DDUDU_BUILD_TESTS=ON \
        -DDUDU_STRICT=ON \
        -DDUDU_WARN_AS_ERROR=ON >/dev/null
    cmake --build "$tool_build_dir" >/dev/null
fi

bench_dir="$repo_root/build/bench_compiler"
mkdir -p "$bench_dir" "$(dirname "$csv_path")"
printf 'case,phase,sample,elapsed_ms,peak_rss_kb,lines,files,status\n' >"$csv_path"
runner="$bench_dir/bench_runner.py"
cat >"$runner" <<'PY'
import resource
import subprocess
import sys

if len(sys.argv) < 4 or sys.argv[2] != "--":
    raise SystemExit("usage: bench_runner.py rss-path -- command [args...]")

rss_path = sys.argv[1]
cmd = sys.argv[3:]
status = subprocess.run(cmd, check=False).returncode
rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
if sys.platform == "darwin":
    rss = int((rss + 1023) / 1024)
with open(rss_path, "w", encoding="utf-8") as handle:
    handle.write(f"{rss}\n")
raise SystemExit(status)
PY

elapsed_ms() {
    local start_ns="$1"
    local end_ns="$2"
    awk -v start="$start_ns" -v end="$end_ns" 'BEGIN { printf "%.3f", (end - start) / 1000000 }'
}

line_count() {
    local path="$1"
    if [[ -f "$path" ]]; then
        wc -l <"$path" | tr -d ' '
    elif [[ -d "$path" ]]; then
        find "$path" -name '*.dd' -type f -print0 | xargs -0r wc -l | awk 'END { print $1 + 0 }'
    else
        echo 0
    fi
}

file_count() {
    local path="$1"
    if [[ -f "$path" ]]; then
        echo 1
    elif [[ -d "$path" ]]; then
        find "$path" -name '*.dd' -type f | wc -l | tr -d ' '
    else
        echo 0
    fi
}

run_case_prepared() {
    local name="$1"
    local phase="$2"
    local source_path="$3"
    local prepare_fn="$4"
    shift 4
    local lines files
    lines="$(line_count "$source_path")"
    files="$(file_count "$source_path")"

    for ((sample = 1; sample <= samples; ++sample)); do
        "$prepare_fn" "$sample"
        local stdout="$bench_dir/${name}_${sample}.out"
        local stderr="$bench_dir/${name}_${sample}.err"
        local rss_file="$bench_dir/${name}_${sample}.rss"
        local start end status
        rm -f "$rss_file"
        start="$(date +%s%N)"
        set +e
        BENCH_CSV_PATH="$csv_path" BENCH_SAMPLE="$sample" BENCH_LINES="$lines" \
            BENCH_FILES="$files" python3 "$runner" "$rss_file" -- "$@" >"$stdout" 2>"$stderr"
        status=$?
        set -e
        end="$(date +%s%N)"
        local ms
        ms="$(elapsed_ms "$start" "$end")"
        local peak_rss
        peak_rss=0
        if [[ -f "$rss_file" ]]; then
            peak_rss="$(tr -d ' ' <"$rss_file")"
        fi
        printf '%s,%s,%s,%s,%s,%s,%s,%s\n' "$name" "$phase" "$sample" "$ms" "$peak_rss" \
            "$lines" "$files" "$status" >>"$csv_path"
        printf '%-32s sample %d/%d %10sms %10sKB status=%s\n' "$name" "$sample" "$samples" \
            "$ms" "$peak_rss" "$status"
        if [[ "$status" -ne 0 ]]; then
            echo "benchmark case failed: $name" >&2
            cat "$stderr" >&2
            exit "$status"
        fi
    done
}

bench_noop() {
    :
}

run_case() {
    local name="$1"
    local phase="$2"
    local source_path="$3"
    shift 3
    run_case_prepared "$name" "$phase" "$source_path" bench_noop "$@"
}

simple="$repo_root/tests/fixtures/simple_program.dd"
native_header="$repo_root/tests/fixtures/array_c_handoff.dd"
multi_project="$repo_root/tests/fixtures/project_backend_auto_modules_native"
multi_entry="$multi_project/main.dd"
incremental_project="$bench_dir/incremental_project"
incremental_entry="$incremental_project/main.dd"
lsp_project="$bench_dir/lsp_project"
lsp_entry="$lsp_project/main.dd"
lsp_probe="$bench_dir/lsp_probe.py"
synthetic_project="$bench_dir/synthetic_project"
synthetic_entry="$synthetic_project/main.dd"
scale_root="$bench_dir/scale_projects"
IFS=',' read -r -a requested_shapes <<<"$shapes"

shape_enabled() {
    local wanted="$1"
    local shape
    for shape in "${requested_shapes[@]}"; do
        shape="$(printf '%s' "$shape" | tr -d '[:space:]')"
        if [[ "$shape" == "$wanted" ]]; then
            return 0
        fi
    done
    return 1
}

validate_shapes() {
    local shape
    for shape in "${requested_shapes[@]}"; do
        shape="$(printf '%s' "$shape" | tr -d '[:space:]')"
        case "$shape" in
            functions|classes|expressions|modules|calls|control|arrays|indexing|generics|matches|operators|native|stdlib|mixed)
                ;;
            *)
                echo "invalid --shapes entry: $shape" >&2
                exit 1
                ;;
        esac
    done
}

validate_shapes

source "$repo_root/scripts/bench_compiler_project_cases.sh"
source "$repo_root/scripts/bench_compiler_core_cases.sh"
source "$repo_root/scripts/bench_compiler_native_cases.sh"

run_case "duc_check_simple" "frontend_check" "$simple" \
    "$tool_build_dir/duc" check "$simple"

run_case "duc_emit_simple" "cpp_emit" "$simple" \
    "$tool_build_dir/duc" emit "$simple" -o "$bench_dir/simple_program.cpp"

"$tool_build_dir/duc" clean-cache "$native_header" >/dev/null 2>&1
run_case "duc_check_native_cold" "native_scan_cold" "$native_header" \
    "$tool_build_dir/duc" check "$native_header"

run_case "duc_check_native_cached" "native_scan_cached" "$native_header" \
    "$tool_build_dir/duc" check "$native_header"

run_case "duc_build_file" "compiler_driver_build" "$simple" \
    "$tool_build_dir/duc" build "$simple" -o "$bench_dir/simple_program" --quiet

run_case "dudu_build_cmake_modules" "cmake_build" "$multi_project" \
    "$tool_build_dir/dudu" build "$multi_entry" --quiet

prepare_incremental_project
"$tool_build_dir/dudu" build "$incremental_entry" --quiet >/dev/null

run_case "dudu_build_cmake_modules_noop" "cmake_noop_build" "$incremental_project" \
    "$tool_build_dir/dudu" build "$incremental_entry" --quiet

run_case_prepared "dudu_build_cmake_modules_changed" "cmake_one_file_changed_build" \
    "$incremental_project" touch_incremental_dep \
    "$tool_build_dir/dudu" build "$incremental_entry" --quiet

prepare_lsp_project
run_case "duc_lsp_diagnostics" "lsp_diagnostics" "$lsp_entry" \
    python3 "$lsp_probe" "$tool_build_dir/duc" "$lsp_entry"

prepare_synthetic_project
run_case "duc_check_synthetic" "frontend_synthetic" "$synthetic_project" \
    "$tool_build_dir/duc" check "$synthetic_entry"

IFS=',' read -r -a requested_line_scales <<<"$line_scales"
for requested_lines in "${requested_line_scales[@]}"; do
    requested_lines="$(printf '%s' "$requested_lines" | tr -d '[:space:]')"
    if [[ -z "$requested_lines" ]]; then
        continue
    fi
    case "$requested_lines" in
        ''|*[!0-9]*)
            echo "invalid --line-scales entry: $requested_lines" >&2
            exit 1
            ;;
    esac
    if shape_enabled "functions"; then
        scaled_entry="$(prepare_scaled_functions_case "$requested_lines")"
        run_case "duc_check_functions_${requested_lines}" "frontend_functions" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "classes"; then
        scaled_entry="$(prepare_scaled_classes_case "$requested_lines")"
        run_case "duc_check_classes_${requested_lines}" "frontend_classes" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "expressions"; then
        scaled_entry="$(prepare_scaled_expressions_case "$requested_lines")"
        run_case "duc_check_expressions_${requested_lines}" "frontend_expressions" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "modules"; then
        scaled_entry="$(prepare_scaled_modules_case "$requested_lines")"
        scaled_project="$(dirname "$scaled_entry")"
        run_case "duc_check_modules_${requested_lines}" "frontend_modules" "$scaled_project" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "calls"; then
        scaled_entry="$(prepare_scaled_calls_case "$requested_lines")"
        run_case "duc_check_calls_${requested_lines}" "frontend_calls" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "control"; then
        scaled_entry="$(prepare_scaled_control_case "$requested_lines")"
        run_case "duc_check_control_${requested_lines}" "frontend_control" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "arrays"; then
        scaled_entry="$(prepare_scaled_arrays_case "$requested_lines")"
        run_case "duc_check_arrays_${requested_lines}" "frontend_arrays" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "indexing"; then
        scaled_entry="$(prepare_scaled_indexing_case "$requested_lines")"
        run_case "duc_check_indexing_${requested_lines}" "frontend_indexing" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "generics"; then
        scaled_entry="$(prepare_scaled_generics_case "$requested_lines")"
        run_case "duc_check_generics_${requested_lines}" "frontend_generics" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "matches"; then
        scaled_entry="$(prepare_scaled_matches_case "$requested_lines")"
        run_case "duc_check_matches_${requested_lines}" "frontend_matches" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "operators"; then
        scaled_entry="$(prepare_scaled_operators_case "$requested_lines")"
        run_case "duc_check_operators_${requested_lines}" "frontend_operators" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "native"; then
        scaled_entry="$(prepare_scaled_native_case "$requested_lines")"
        scaled_project="$(dirname "$scaled_entry")"
        run_case "duc_check_native_${requested_lines}" "frontend_native" "$scaled_project" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "stdlib"; then
        scaled_entry="$(prepare_scaled_stdlib_case "$requested_lines")"
        run_case "duc_check_stdlib_${requested_lines}" "frontend_stdlib" "$scaled_entry" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
    if shape_enabled "mixed"; then
        scaled_entry="$(prepare_scaled_mixed_case "$requested_lines")"
        scaled_project="$(dirname "$scaled_entry")"
        run_case "duc_check_mixed_${requested_lines}" "frontend_mixed" "$scaled_project" \
            "$tool_build_dir/duc" check "$scaled_entry"
    fi
done

echo
echo "summary:"
awk -F',' '
NR > 1 {
    key = $1 "," $2
    count[key] += 1
    sum[key] += $4
    rss[key] += $5
    lines[key] = $6
    files[key] = $7
}
END {
    for (key in count) {
        split(key, parts, ",")
        mean_ms = sum[key] / count[key]
        lines_per_second = mean_ms > 0 ? (lines[key] * 1000.0 / mean_ms) : 0
        printf "%-32s phase=%-34s mean_ms=%10.3f lines_per_second=%12.0f mean_peak_rss_kb=%10.0f lines=%s files=%s\n",
            parts[1], parts[2], mean_ms, lines_per_second, rss[key] / count[key],
            lines[key], files[key]
    }
}' "$csv_path" | sort

echo
echo "csv: $csv_path"
