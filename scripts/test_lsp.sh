#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - "$repo_root" <<'PY'
import json
import subprocess
import sys

repo_root = sys.argv[1]

def packet(obj):
    body = json.dumps(obj, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}"

def read_packets(data):
    out = []
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
        if length is None:
            raise AssertionError(f"missing Content-Length in {headers!r}")
        body_start = header_end + 4
        body_end = body_start + length
        out.append(json.loads(data[body_start:body_end]))
        cursor = body_end
    return out

uri = "file:///tmp/dudu_lsp_bad.dd"
messages = [
    packet({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "def main() -> i32:   \n    return True\n",
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/formatting",
            "params": {"textDocument": {"uri": uri}, "options": {"tabSize": 4}},
        }
    ),
    packet({"jsonrpc": "2.0", "id": 3, "method": "shutdown", "params": None}),
    packet({"jsonrpc": "2.0", "method": "exit", "params": None}),
]

proc = subprocess.run(
    [f"{repo_root}/build/duc", "lsp"],
    input="".join(messages).encode(),
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    timeout=5,
    check=True,
)
if proc.stderr:
    raise AssertionError(proc.stderr.decode())

responses = read_packets(proc.stdout)
initialize = next(item for item in responses if item.get("id") == 1)
assert initialize["result"]["capabilities"]["textDocumentSync"] == 2
assert initialize["result"]["capabilities"]["documentFormattingProvider"] is True

diagnostics = next(item for item in responses if item.get("method") == "textDocument/publishDiagnostics")
diag = diagnostics["params"]["diagnostics"][0]
assert diag["source"] == "dudu/sema"
assert "return type mismatch" in diag["message"]

formatting = next(item for item in responses if item.get("id") == 2)
assert formatting["result"][0]["newText"] == "def main() -> i32:\n    return True\n"

shutdown = next(item for item in responses if item.get("id") == 3)
assert shutdown["result"] is None
PY

echo "lsp smoke checks passed"
