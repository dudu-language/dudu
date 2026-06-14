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
source = "\n".join(
    [
        "class Player:",
        "    hp: i32",
        "",
        "def add(a: i32, b: i32) -> i32:",
        "    return a + b",
        "",
        "def main() -> i32:   ",
        "    return True",
        "",
    ]
)
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
                    "text": source,
                }
            },
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
    packet(
        {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "textDocument/hover",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 5}},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "textDocument/definition",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 5}},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 5,
            "method": "textDocument/formatting",
            "params": {"textDocument": {"uri": uri}, "options": {"tabSize": 4}},
        }
    ),
    packet({"jsonrpc": "2.0", "id": 6, "method": "shutdown", "params": None}),
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

symbols = next(item for item in responses if item.get("id") == 2)
symbol_names = [item["name"] for item in symbols["result"]]
assert "Player" in symbol_names
assert "add" in symbol_names
assert "main" in symbol_names

hover = next(item for item in responses if item.get("id") == 3)
assert "def add" in hover["result"]["contents"]["value"]

definition = next(item for item in responses if item.get("id") == 4)
assert definition["result"]["range"]["start"]["line"] == 3

formatting = next(item for item in responses if item.get("id") == 5)
assert "def main() -> i32:\n    return True\n" in formatting["result"][0]["newText"]

shutdown = next(item for item in responses if item.get("id") == 6)
assert shutdown["result"] is None
PY

echo "lsp smoke checks passed"
