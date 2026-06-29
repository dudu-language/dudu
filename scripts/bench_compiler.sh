#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
samples=3
csv_path="$repo_root/build/bench_compiler/compiler_bench.csv"
build_tools=1
build_type="Debug"
line_scales="1000,5000,10000"
shapes="functions,classes,expressions,modules,calls,control,arrays,indexing,generics,matches,operators,mixed"

usage() {
    cat <<'EOF'
usage: scripts/bench_compiler.sh [--samples N] [--csv path] [--build-type Debug|Release] [--line-scales list] [--shapes list] [--no-build]

Measures Dudu compiler and project-driver latency. This is an explicit
developer benchmark, not part of the fast correctness loop.

--line-scales accepts a comma-separated list of approximate generated Dudu
source line counts, for example:
  --line-scales 10000,50000,100000,200000,500000,1000000

--shapes accepts a comma-separated list of generated frontend stress shapes:
  functions,classes,expressions,modules,calls,control,arrays,indexing,generics,matches,operators,stdlib,mixed

The stdlib shape is intentionally not in the default set because real C++
standard-library header scanning is much slower than pure Dudu frontend
shapes. Select it explicitly when measuring native interop throughput.

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
        python3 "$runner" "$rss_file" -- "$@" >"$stdout" 2>"$stderr"
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
            functions|classes|expressions|modules|calls|control|arrays|indexing|generics|matches|operators|stdlib|mixed)
                ;;
            *)
                echo "invalid --shapes entry: $shape" >&2
                exit 1
                ;;
        esac
    done
}

prepare_incremental_project() {
    rm -rf "$incremental_project"
    mkdir -p "$incremental_project"
    cp "$multi_project"/*.dd "$multi_project"/*.hpp "$incremental_project"/
    cat >"$incremental_project/dudu.toml" <<'EOF'
name = "bench_incremental_modules"
entry = "main.dd"
build_dir = "build"

[cc]
include_dirs = ["."]
EOF
}

touch_incremental_dep() {
    local sample="$1"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/native\.add\(20, 22\)/native.add(21, 21)/g' "$incremental_project/dep.dd"
    else
        perl -0pi -e 's/native\.add\(21, 21\)/native.add(20, 22)/g' "$incremental_project/dep.dd"
    fi
}

prepare_lsp_project() {
    rm -rf "$lsp_project"
    mkdir -p "$lsp_project"
    cat >"$lsp_entry" <<'EOF'
class Player:
    hp: i32

def add(a: i32, b: i32) -> i32:
    return a + b

def main() -> i32:
    player = Player(42)
    value = add(player.hp, 8)
    return True
EOF
    cat >"$lsp_probe" <<'PY'
import json
import pathlib
import subprocess
import sys

duc = sys.argv[1]
entry = pathlib.Path(sys.argv[2]).resolve()
uri = entry.as_uri()
source = entry.read_text()

def packet(obj):
    body = json.dumps(obj, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}"

def read_packets(data):
    packets = []
    cursor = 0
    while cursor < len(data):
        header_end = data.find(b"\r\n\r\n", cursor)
        if header_end < 0:
            break
        headers = data[cursor:header_end].decode()
        length = None
        for line in headers.split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
                break
        if length is None:
            raise RuntimeError(f"missing Content-Length in {headers!r}")
        body_start = header_end + 4
        body_end = body_start + length
        packets.append(json.loads(data[body_start:body_end]))
        cursor = body_end
    return packets

messages = [
    packet({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": entry.parent.as_uri()}}),
    packet({"jsonrpc": "2.0", "method": "initialized", "params": {}}),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": source,
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didSave",
            "params": {"textDocument": {"uri": uri}},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/documentSymbol",
            "params": {"textDocument": {"uri": uri}},
        }
    ),
    packet({"jsonrpc": "2.0", "id": 3, "method": "shutdown", "params": None}),
    packet({"jsonrpc": "2.0", "method": "exit", "params": None}),
]

proc = subprocess.run(
    [duc, "lsp"],
    input="".join(messages).encode(),
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    timeout=5,
    check=False,
)
if proc.returncode != 0:
    raise RuntimeError(proc.stderr.decode(errors="replace"))

packets = read_packets(proc.stdout)
diagnostics = [
    item for item in packets
    if item.get("method") == "textDocument/publishDiagnostics"
    and item.get("params", {}).get("uri") == uri
]
if not diagnostics:
    raise RuntimeError("LSP did not publish diagnostics for benchmark document")
diagnostic_messages = [
    diagnostic.get("message", "")
    for diagnostic in diagnostics[-1].get("params", {}).get("diagnostics", [])
]
if not any("return type mismatch" in message for message in diagnostic_messages):
    raise RuntimeError(f"unexpected diagnostics: {diagnostic_messages!r}")

symbol_response = next((item for item in packets if item.get("id") == 2), None)
if symbol_response is None or "result" not in symbol_response:
    raise RuntimeError("LSP did not answer documentSymbol")
symbol_names = {item.get("name") for item in symbol_response["result"]}
if not {"Player", "add", "main"}.issubset(symbol_names):
    raise RuntimeError(f"unexpected document symbols: {symbol_response['result']!r}")
PY
}

prepare_synthetic_project() {
    rm -rf "$synthetic_project"
    mkdir -p "$synthetic_project"
    : >"$synthetic_entry"
    for module in $(seq 0 11); do
        printf 'import mod%02d\n' "$module" >>"$synthetic_entry"
    done
    {
        printf '\n'
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
    } >>"$synthetic_entry"
    for module in $(seq 0 11); do
        for func in $(seq 0 15); do
            printf '    total += mod%02d.mod%02d_func%02d(%d)\n' \
                "$module" "$module" "$func" "$func" >>"$synthetic_entry"
        done
    done
    printf '    return total\n' >>"$synthetic_entry"

    for module in $(seq 0 11); do
        local file
        file="$(printf '%s/mod%02d.dd' "$synthetic_project" "$module")"
        : >"$file"
        for func in $(seq 0 15); do
            cat >>"$file" <<EOF
def mod$(printf '%02d' "$module")_func$(printf '%02d' "$func")(value: i32) -> i32:
    running: i32 = value
    running += $(($module + $func))
    if running % 2 == 0:
        return running / 2
    return running * 3 + 1

EOF
        done
    done
}

prepare_scaled_functions_case() {
    local requested_lines="$1"
    local project="$scale_root/functions_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    # Each generated helper contributes five lines. The main function keeps
    # enough references alive to exercise name lookup without making C++ compile
    # time part of this frontend benchmark.
    local helpers=$((requested_lines / 5))
    if [[ "$helpers" -lt 1 ]]; then
        helpers=1
    fi
    : >"$entry"
    for ((i = 0; i < helpers; ++i)); do
        cat >>"$entry" <<EOF
def generated_func_$i(value: i32) -> i32:
    total: i32 = value
    total += $i
    return total

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$helpers"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += generated_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_classes_case() {
    local requested_lines="$1"
    local project="$scale_root/classes_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local classes=$((requested_lines / 9))
    if [[ "$classes" -lt 1 ]]; then
        classes=1
    fi
    : >"$entry"
    for ((i = 0; i < classes; ++i)); do
        cat >>"$entry" <<EOF
class BenchType$i:
    value: i32

    def bump(self, delta: i32) -> i32:
        self.value += delta
        return self.value

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$classes"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    item_%d = BenchType%d(%d)\n' "$i" "$i" "$i"
            printf '    total += item_%d.bump(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_expressions_case() {
    local requested_lines="$1"
    local project="$scale_root/expressions_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local statements=$((requested_lines - 4))
    if [[ "$statements" -lt 1 ]]; then
        statements=1
    fi
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 1\n'
        for ((i = 0; i < statements; ++i)); do
            printf '    total = (((total + %d) * 3) - ((%d + total) / 2)) + (%d %% 17)\n' "$i" "$i" "$i"
        done
        printf '    return total\n'
    } >"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_modules_case() {
    local requested_lines="$1"
    local project="$scale_root/modules_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local module_count=16
    local funcs_per_module=$((requested_lines / (module_count * 7)))
    if [[ "$funcs_per_module" -lt 1 ]]; then
        funcs_per_module=1
    fi

    : >"$entry"
    for ((module = 0; module < module_count; ++module)); do
        printf 'import mod%02d\n' "$module" >>"$entry"
    done
    {
        printf '\n'
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
    } >>"$entry"
    for ((module = 0; module < module_count; ++module)); do
        local call_limit="$funcs_per_module"
        if [[ "$call_limit" -gt 16 ]]; then
            call_limit=16
        fi
        for ((func = 0; func < call_limit; ++func)); do
            printf '    total += mod%02d.mod%02d_func%03d(%d)\n' \
                "$module" "$module" "$func" "$func" >>"$entry"
        done
    done
    printf '    return total\n' >>"$entry"

    for ((module = 0; module < module_count; ++module)); do
        local file
        file="$(printf '%s/mod%02d.dd' "$project" "$module")"
        : >"$file"
        for ((func = 0; func < funcs_per_module; ++func)); do
            cat >>"$file" <<EOF
def mod$(printf '%02d' "$module")_func$(printf '%03d' "$func")(value: i32) -> i32:
    running: i32 = value
    running += $module
    running += $func
    return running

EOF
        done
    done
    printf '%s\n' "$entry"
}

prepare_scaled_calls_case() {
    local requested_lines="$1"
    local project="$scale_root/calls_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local helpers=$((requested_lines / 6))
    if [[ "$helpers" -lt 1 ]]; then
        helpers=1
    fi
    : >"$entry"
    for ((i = 0; i < helpers; ++i)); do
        cat >>"$entry" <<EOF
def call_leaf_$i(left: i32, right: i32) -> i32:
    value: i32 = left + right
    value += $i
    return value

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$helpers"
        if [[ "$limit" -gt 512 ]]; then
            limit=512
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total = call_leaf_%d(call_leaf_%d(total, %d), total)\n' "$i" "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_control_case() {
    local requested_lines="$1"
    local project="$scale_root/control_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 12))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    : >"$entry"
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def control_func_$i(value: i32) -> i32:
    total: i32 = value
    for index: i32 in range(4):
        if total % 2 == 0:
            total += index + $i
        else:
            total -= index
    while total < $((i + 16)):
        total += 1
    return total

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 256 ]]; then
            limit=256
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += control_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_arrays_case() {
    local requested_lines="$1"
    local project="$scale_root/arrays_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 12))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    : >"$entry"
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def array_func_$i(values: &array[i32][4]) -> i32:
    total: i32 = 0
    for index: i32 in range(4):
        total += values[index]
    values[0] = total + $i
    return values[0] + values[1]

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    values: array[i32] = [1, 2, 3, 4]\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 256 ]]; then
            limit=256
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += array_func_%d(values)\n' "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_indexing_case() {
    local requested_lines="$1"
    local project="$scale_root/indexing_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 24))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    cat >"$entry" <<'EOF'
class Vec2:
    x: i32
    y: i32


class Color4:
    r: i32
    g: i32
    b: i32
    a: i32


def sum_span(values: strided_span[i32]) -> i32:
    total: i32 = 0
    for value: i32 in values:
        total += value
    return total

EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def indexing_func_$i(seed: i32) -> i32:
    matrix: array[i32] = [
        [seed, seed + 1, seed + 2],
        [seed + 3, seed + 4, seed + 5],
    ]
    image: array[i32] = [
        [[seed, seed + 1], [seed + 2, seed + 3]],
        [[seed + 4, seed + 5], [seed + 6, seed + 7]],
    ]
    row: array[i32][3] = matrix[1]
    column: strided_span[i32] = matrix[:, 1]
    channel: strided_span[i32] = image[:, :, 0]
    pos: Vec2 = Vec2(seed, seed + 1)
    pos.yx = Vec2(seed + 2, seed + 3)
    color: Color4 = Color4(seed, seed + 1, seed + 2, seed + 3)
    color.bgra = Color4(seed + 6, seed + 5, seed + 4, seed + 7)
    return row[0] + row[2] + sum_span(column) + sum_span(channel) + pos.x + pos.y + color.r + color.a + $i

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += indexing_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_generics_case() {
    local requested_lines="$1"
    local project="$scale_root/generics_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 9))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    cat >"$entry" <<'EOF'
class BenchBox[T]:
    value: T

    def get(self) -> T:
        return self.value

def bench_identity[T](value: T) -> T:
    return value

EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def generic_func_$i(value: i32) -> i32:
    box: BenchBox[i32] = BenchBox[i32](bench_identity[i32](value + $i))
    return box.get()

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 256 ]]; then
            limit=256
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += generic_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_matches_case() {
    local requested_lines="$1"
    local project="$scale_root/matches_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 28))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    cat >"$entry" <<'EOF'
enum Message:
    Quit

    Move:
        x: i32
        y: i32

    Write(str)

    Scale(i32)


EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def match_func_$i(msg: Message, seed: i32) -> i32:
    match msg:
        case Message.Quit:
            return seed + $i
        case Message.Move(x, y) if x > y:
            return x - y + seed
        case Message.Move(x, y):
            return x + y + $i
        case Message.Write(text) if len(text) > 3:
            return i32(len(text)) + seed
        case Message.Write(text):
            return i32(len(text)) + $i
        case Message.Scale(value):
            return value * 2 + seed

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        printf '    moved: Message = Message.Move(x=20, y=19)\n'
        printf '    text: Message = Message.Write("bench")\n'
        printf '    scale: Message = Message.Scale(7)\n'
        local limit="$funcs"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += match_func_%d(moved, %d)\n' "$i" "$i"
            printf '    total += match_func_%d(text, %d)\n' "$i" "$i"
            printf '    total += match_func_%d(scale, %d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_operators_case() {
    local requested_lines="$1"
    local project="$scale_root/operators_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 24))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi
    cat >"$entry" <<'EOF'
class Vec2:
    x: i32
    y: i32

    @operator("+")
    def add(self, other: Vec2) -> Vec2:
        return Vec2(self.x + other.x, self.y + other.y)

    @operator("*")
    def mul_vec(self, other: Vec2) -> Vec2:
        return Vec2(self.x * other.x, self.y * other.y)

    @operator("*")
    def mul_scalar(self, value: i32) -> Vec2:
        return Vec2(self.x * value, self.y * value)

    @operator("==")
    def same(self, other: Vec2) -> bool:
        return self.x == other.x and self.y == other.y


class Tensor2:
    values: array[i32][4]

    @operator("[]")
    def at(self, index: i32) -> i32:
        return self.values[index]

    @operator("[]=")
    def put(self, index: i32, value: i32):
        self.values[index] = value


EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def operator_func_$i(seed: i32) -> i32:
    a: Vec2 = Vec2(seed + 1, seed + 2)
    b: Vec2 = Vec2(seed + 3, seed + 4)
    c = (a + b) * 2
    d = a * b
    values: array[i32] = [seed, seed + 1, seed + 2, seed + 3]
    tensor = Tensor2(values)
    tensor[1] = c.x + d.y + $i
    if c == d:
        return tensor[0]
    return tensor[1] + tensor[2]

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += operator_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_stdlib_case() {
    local requested_lines="$1"
    local project="$scale_root/stdlib_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local funcs=$((requested_lines / 26))
    if [[ "$funcs" -lt 1 ]]; then
        funcs=1
    fi

    cat >"$entry" <<'EOF'
import cpp "algorithm" as std
import cpp "numeric" as std
import cpp "string" as std
import cpp "tuple" as std
import cpp "unordered_map" as std
import cpp "utility" as std
import cpp "vector" as std

EOF
    for ((i = 0; i < funcs; ++i)); do
        cat >>"$entry" <<EOF
def stdlib_func_$i(seed: i32) -> i32:
    values: std.vector[i32]
    values.push_back(seed)
    values.push_back(seed + 3)
    values.push_back($i)
    values.push_back(seed + 1)
    std.sort(values.begin(), values.end())
    total: i32 = std.accumulate(values.begin(), values.end(), 0)
    names: std.vector[std.string]
    names.push_back(std.string("alpha"))
    names.push_back(std.string("beta"))
    scores: std.unordered_map[std.string, i32]
    scores[names[0]] = total
    scores[names[1]] = total + $i
    pair: std.pair[std.string, i32] = std.make_pair(std.string("score"), scores[names[1]])
    tup: std.tuple[std.string, i32] = std.make_tuple(pair.first, pair.second)
    if std.get[1](tup) != pair.second:
        return 0
    return scores[std.string("alpha")] + std.get[1](tup)

EOF
    done
    {
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
        local limit="$funcs"
        if [[ "$limit" -gt 128 ]]; then
            limit=128
        fi
        for ((i = 0; i < limit; ++i)); do
            printf '    total += stdlib_func_%d(%d)\n' "$i" "$i"
        done
        printf '    return total\n'
    } >>"$entry"
    printf '%s\n' "$entry"
}

prepare_scaled_mixed_case() {
    local requested_lines="$1"
    local project="$scale_root/mixed_$requested_lines"
    local entry="$project/main.dd"
    rm -rf "$project"
    mkdir -p "$project"

    local module_count=8
    local funcs_per_module=$((requested_lines / (module_count * 22)))
    if [[ "$funcs_per_module" -lt 1 ]]; then
        funcs_per_module=1
    fi

    : >"$entry"
    for ((module = 0; module < module_count; ++module)); do
        printf 'import mix%02d\n' "$module" >>"$entry"
    done
    {
        printf '\n'
        printf 'def main() -> i32:\n'
        printf '    total: i32 = 0\n'
    } >>"$entry"
    for ((module = 0; module < module_count; ++module)); do
        local call_limit="$funcs_per_module"
        if [[ "$call_limit" -gt 32 ]]; then
            call_limit=32
        fi
        for ((func = 0; func < call_limit; ++func)); do
            printf '    total += mix%02d.mix%02d_func_%03d(%d)\n' \
                "$module" "$module" "$func" "$func" >>"$entry"
        done
    done
    printf '    return total\n' >>"$entry"

    for ((module = 0; module < module_count; ++module)); do
        local file
        file="$(printf '%s/mix%02d.dd' "$project" "$module")"
        cat >"$file" <<EOF
class MixBox$(printf '%02d' "$module"):
    value: i32
    scale: i32

    def bump(self, amount: i32) -> i32:
        self.value += amount * self.scale
        return self.value

def mix$(printf '%02d' "$module")_identity[T](value: T) -> T:
    return value

EOF
        for ((func = 0; func < funcs_per_module; ++func)); do
            cat >>"$file" <<EOF
def mix$(printf '%02d' "$module")_func_$(printf '%03d' "$func")(seed: i32) -> i32:
    box: MixBox$(printf '%02d' "$module") = MixBox$(printf '%02d' "$module")(seed + $func, $((module + 1)))
    values: array[i32] = [seed, seed + 1, seed + 2, seed + 3]
    total: i32 = 0
    for index: i32 in range(4):
        total += values[index]
        if total % 2 == 0:
            total += box.bump(index)
        else:
            total -= index
    return mix$(printf '%02d' "$module")_identity[i32](total)

EOF
        done
    done
    printf '%s\n' "$entry"
}

validate_shapes

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
