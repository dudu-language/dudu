# shellcheck shell=bash

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
import os
import pathlib
import subprocess
import sys
import time

duc = sys.argv[1]
entry = pathlib.Path(sys.argv[2]).resolve()
uri = entry.as_uri()
source = entry.read_text()

def frame_message(obj):
    body = json.dumps(obj, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}"

def read_message(stream):
    header = bytearray()
    while not header.endswith(b"\r\n\r\n"):
        chunk = stream.read(1)
        if not chunk:
            raise RuntimeError("LSP server closed stdout")
        header.extend(chunk)
    headers = header[:-4].decode()
    length = None
    for line in headers.split("\r\n"):
        if line.lower().startswith("content-length:"):
            length = int(line.split(":", 1)[1].strip())
            break
    if length is None:
        raise RuntimeError(f"missing Content-Length in {headers!r}")
    return json.loads(stream.read(length))

def send_message(proc, obj):
    proc.stdin.write(frame_message(obj).encode())
    proc.stdin.flush()

def wait_response(proc, request_id):
    while True:
        message = read_message(proc.stdout)
        if message.get("id") == request_id:
            return message

def wait_diagnostics(proc):
    while True:
        message = read_message(proc.stdout)
        if (
            message.get("method") == "textDocument/publishDiagnostics"
            and message.get("params", {}).get("uri") == uri
        ):
            return message

def csv_escape(value):
    text = str(value)
    if any(ch in text for ch in ",\"\n"):
        return '"' + text.replace('"', '""') + '"'
    return text

def record_detail(case, phase, elapsed_ms, status=0):
    csv_path = os.environ.get("BENCH_CSV_PATH")
    if not csv_path:
        return
    sample = os.environ.get("BENCH_SAMPLE", "0")
    lines = os.environ.get("BENCH_LINES", "0")
    files = os.environ.get("BENCH_FILES", "0")
    row = [case, phase, sample, f"{elapsed_ms:.3f}", "0", lines, files, status]
    with open(csv_path, "a", encoding="utf-8") as handle:
        handle.write(",".join(csv_escape(item) for item in row) + "\n")

def timed_request(proc, case, phase, request, validator):
    start = time.perf_counter()
    send_message(proc, request)
    response = wait_response(proc, request["id"])
    elapsed = (time.perf_counter() - start) * 1000.0
    validator(response)
    record_detail(case, phase, elapsed)
    return response

def timed_diagnostics(proc, case, phase, request, validator):
    start = time.perf_counter()
    send_message(proc, request)
    response = wait_diagnostics(proc)
    elapsed = (time.perf_counter() - start) * 1000.0
    validator(response)
    record_detail(case, phase, elapsed)
    return response

next_request_id = 1
version = 1

def request(method, params):
    global next_request_id
    request_id = next_request_id
    next_request_id += 1
    return {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}

def notification(method, params):
    return {"jsonrpc": "2.0", "method": method, "params": params}

def document_params():
    return {"textDocument": {"uri": uri}}

def invalidate_document(proc):
    global version
    version += 1
    send_message(
        proc,
        notification(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": version},
                "contentChanges": [{"text": source}],
            },
        ),
    )

def validate_diagnostics(message):
    diagnostics = message.get("params", {}).get("diagnostics", [])
    diagnostic_messages = [diagnostic.get("message", "") for diagnostic in diagnostics]
    if not any("return type mismatch" in item for item in diagnostic_messages):
        raise RuntimeError(f"unexpected diagnostics: {diagnostic_messages!r}")

def validate_document_symbols(response):
    if "result" not in response:
        raise RuntimeError(f"LSP did not answer documentSymbol: {response!r}")
    symbol_names = {item.get("name") for item in response["result"]}
    if not {"Player", "add", "main"}.issubset(symbol_names):
        raise RuntimeError(f"unexpected document symbols: {response['result']!r}")

def validate_workspace_add(response):
    if not any(item.get("name") == "add" for item in response.get("result", [])):
        raise RuntimeError(f"unexpected workspace add symbols: {response!r}")

def validate_workspace_player(response):
    if not any(item.get("name") == "Player" for item in response.get("result", [])):
        raise RuntimeError(f"unexpected workspace Player symbols: {response!r}")

def validate_references(response):
    if len(response.get("result", [])) < 2:
        raise RuntimeError(f"unexpected references response: {response!r}")

def validate_definition(response):
    if not response.get("result"):
        raise RuntimeError(f"unexpected definition response: {response!r}")

def validate_hover(response):
    contents = response.get("result", {}).get("contents")
    if not contents:
        raise RuntimeError(f"unexpected hover response: {response!r}")

def validate_completion(response):
    labels = [item.get("label") for item in response.get("result", [])]
    if "hp" not in labels:
        raise RuntimeError(f"unexpected completion response: {response!r}")

def run_cold_warm(proc, case_base, phase_base, request_factory, validator):
    invalidate_document(proc)
    timed_request(proc, f"{case_base}_cold", f"{phase_base}_cold", request_factory(), validator)
    timed_request(proc, f"{case_base}_warm", f"{phase_base}_warm", request_factory(), validator)

proc = subprocess.Popen(
    [str(pathlib.Path(duc).parent / "dudu-lsp")],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
try:
    initialize = request("initialize", {"rootUri": entry.parent.as_uri()})
    send_message(proc, initialize)
    wait_response(proc, initialize["id"])
    send_message(proc, notification("initialized", {}))
    send_message(
        proc,
        notification(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dudu",
                    "version": version,
                    "text": source,
                }
            },
        ),
    )

    timed_diagnostics(
        proc,
        "duc_lsp_diagnostics_cold",
        "lsp_diagnostics_cold",
        notification("textDocument/didSave", document_params()),
        validate_diagnostics,
    )
    timed_request(
        proc,
        "duc_lsp_document_symbols_warm",
        "lsp_document_symbols_warm",
        request("textDocument/documentSymbol", document_params()),
        validate_document_symbols,
    )
    run_cold_warm(
        proc,
        "duc_lsp_workspace_symbol_add",
        "lsp_workspace_symbol_add",
        lambda: request("workspace/symbol", {"query": "add"}),
        validate_workspace_add,
    )
    run_cold_warm(
        proc,
        "duc_lsp_workspace_symbol_player",
        "lsp_workspace_symbol_player",
        lambda: request("workspace/symbol", {"query": "Player"}),
        validate_workspace_player,
    )
    run_cold_warm(
        proc,
        "duc_lsp_references",
        "lsp_references",
        lambda: request(
            "textDocument/references",
            {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 13}},
        ),
        validate_references,
    )
    run_cold_warm(
        proc,
        "duc_lsp_definition",
        "lsp_definition",
        lambda: request(
            "textDocument/definition",
            {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 13}},
        ),
        validate_definition,
    )
    run_cold_warm(
        proc,
        "duc_lsp_hover",
        "lsp_hover",
        lambda: request(
            "textDocument/hover",
            {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 13}},
        ),
        validate_hover,
    )
    run_cold_warm(
        proc,
        "duc_lsp_completion",
        "lsp_completion",
        lambda: request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 23}},
        ),
        validate_completion,
    )

    shutdown = request("shutdown", None)
    send_message(proc, shutdown)
    wait_response(proc, shutdown["id"])
    send_message(proc, notification("exit", None))
    stderr = proc.stderr.read().decode(errors="replace")
    proc.wait(timeout=5)
    if proc.returncode != 0:
        raise RuntimeError(stderr)
finally:
    if proc.poll() is None:
        proc.kill()
        proc.wait(timeout=5)
PY
}
