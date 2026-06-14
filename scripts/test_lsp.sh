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
native_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_native.dd"
source = "\n".join(
    [
        "class Player:",
        "    hp: i32",
        "",
        "def add(a: i32, b: i32) -> i32:",
        "    return a + b",
        "",
        "def main() -> i32:   ",
        "    value: i32 = add(1, 2)",
        "    return True",
        "",
    ]
)
native_source = "\n".join(
    [
        'import c "native_headers/simple_c.h" as dudu_native',
        "",
        "def main() -> i32:",
        "    return dudu_native.dudu_native_add(20, 22)",
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
            "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 4}},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "textDocument/signatureHelp",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 7, "character": 25}},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 7,
            "method": "textDocument/formatting",
            "params": {"textDocument": {"uri": uri}, "options": {"tabSize": 4}},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": native_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": native_source,
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 8,
            "method": "textDocument/documentSymbol",
            "params": {"textDocument": {"uri": native_uri}},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 9,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 3, "character": 30},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 10,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 3, "character": 30},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 11,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 3, "character": 11},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 12,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 3, "character": 44},
            },
        }
    ),
    packet({"jsonrpc": "2.0", "id": 20, "method": "shutdown", "params": None}),
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

completion = next(item for item in responses if item.get("id") == 5)
completion_labels = [item["label"] for item in completion["result"]]
assert "return" in completion_labels
assert "i32" in completion_labels
assert "add" in completion_labels
assert "Player" in completion_labels

signature = next(item for item in responses if item.get("id") == 6)
assert "def add(a: i32, b: i32) -> i32" in signature["result"]["signatures"][0]["label"]
assert signature["result"]["activeParameter"] == 1

formatting = next(item for item in responses if item.get("id") == 7)
assert "def main() -> i32:\n    value: i32 = add(1, 2)\n    return True\n" in formatting["result"][0]["newText"]

native_symbols = next(item for item in responses if item.get("id") == 8)
native_symbol_names = [item["name"] for item in native_symbols["result"]]
assert "dudu_native.dudu_native_add" in native_symbol_names

native_hover = next(item for item in responses if item.get("id") == 9)
assert "dudu_native.dudu_native_add(i32, i32) -> i32" in native_hover["result"]["contents"]["value"]

native_definition = next(item for item in responses if item.get("id") == 10)
assert native_definition["result"]["uri"].endswith("/tests/fixtures/native_headers/simple_c.h")
assert native_definition["result"]["range"]["start"]["line"] == 20

native_completion = next(item for item in responses if item.get("id") == 11)
native_completion_labels = [item["label"] for item in native_completion["result"]]
assert "dudu_native.dudu_native_add" in native_completion_labels

native_signature = next(item for item in responses if item.get("id") == 12)
assert "dudu_native.dudu_native_add(i32, i32) -> i32" in native_signature["result"]["signatures"][0]["label"]
assert native_signature["result"]["activeParameter"] == 1

shutdown = next(item for item in responses if item.get("id") == 20)
assert shutdown["result"] is None
PY

echo "lsp smoke checks passed"
