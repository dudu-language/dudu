#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
samples=3
csv_path="$repo_root/build/bench_compiler/compiler_bench.csv"
build_tools=1

usage() {
    cat <<'EOF'
usage: scripts/bench_compiler.sh [--samples N] [--csv path] [--no-build]

Measures Dudu compiler and project-driver latency. This is an explicit
developer benchmark, not part of the fast correctness loop.
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

if [[ "$build_tools" -eq 1 ]]; then
    "$repo_root/scripts/build.sh" >/dev/null
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

run_case "duc_check_simple" "frontend_check" "$simple" \
    "$repo_root/build/duc" check "$simple"

run_case "duc_emit_simple" "cpp_emit" "$simple" \
    "$repo_root/build/duc" emit "$simple" -o "$bench_dir/simple_program.cpp"

"$repo_root/build/duc" clean-cache "$native_header" >/dev/null 2>&1
run_case "duc_check_native_cold" "native_scan_cold" "$native_header" \
    "$repo_root/build/duc" check "$native_header"

run_case "duc_check_native_cached" "native_scan_cached" "$native_header" \
    "$repo_root/build/duc" check "$native_header"

run_case "duc_build_direct" "direct_build" "$simple" \
    "$repo_root/build/duc" build "$simple" -o "$bench_dir/simple_program" --quiet

run_case "dudu_build_cmake_modules" "cmake_build" "$multi_project" \
    "$repo_root/build/dudu" build "$multi_entry" --quiet

prepare_incremental_project
"$repo_root/build/dudu" build "$incremental_entry" --quiet >/dev/null

run_case "dudu_build_cmake_modules_noop" "cmake_noop_build" "$incremental_project" \
    "$repo_root/build/dudu" build "$incremental_entry" --quiet

run_case_prepared "dudu_build_cmake_modules_changed" "cmake_one_file_changed_build" \
    "$incremental_project" touch_incremental_dep \
    "$repo_root/build/dudu" build "$incremental_entry" --quiet

prepare_lsp_project
run_case "duc_lsp_diagnostics" "lsp_diagnostics" "$lsp_entry" \
    python3 "$lsp_probe" "$repo_root/build/duc" "$lsp_entry"

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
        printf "%-32s phase=%-34s mean_ms=%10.3f mean_peak_rss_kb=%10.0f lines=%s files=%s\n",
            parts[1], parts[2], sum[key] / count[key], rss[key] / count[key],
            lines[key], files[key]
    }
}' "$csv_path" | sort

echo
echo "csv: $csv_path"
