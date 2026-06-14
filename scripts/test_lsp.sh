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
missing_native_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_missing_native.dd"
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
        "    player: Player = Player(3)",
        "    player.hp",
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
        "# dudu_native.DUDU_NATIVE_SCALE",
        "# workspace_helper",
        "",
    ]
)
missing_native_source = "\n".join(
    [
        'import c "./native_headers/does_not_exist.h"',
        "",
        "def main() -> i32:",
        "    return 0",
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
            "id": 21,
            "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 9, "character": 11}},
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
            "id": 13,
            "method": "textDocument/references",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 7, "character": 18}},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 16,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {"uri": uri},
                "position": {"line": 3, "character": 5},
                "newName": "sum_values",
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 17,
            "method": "textDocument/codeAction",
            "params": {
                "textDocument": {"uri": uri},
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 0},
                },
                "context": {"diagnostics": []},
            },
        }
    ),
    packet({"jsonrpc": "2.0", "id": 14, "method": "workspace/symbol", "params": {"query": "add"}}),
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
    packet(
        {
            "jsonrpc": "2.0",
            "id": 15,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 4, "character": 17},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 18,
            "method": "textDocument/references",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 5, "character": 5},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 19,
            "method": "workspace/symbol",
            "params": {"query": "workspace_helper"},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": missing_native_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": missing_native_source,
                }
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
assert initialize["result"]["capabilities"]["referencesProvider"] is True
assert initialize["result"]["capabilities"]["renameProvider"] is True
assert initialize["result"]["capabilities"]["codeActionProvider"] is True
assert initialize["result"]["capabilities"]["workspaceSymbolProvider"] is True

diagnostics = next(item for item in responses if item.get("method") == "textDocument/publishDiagnostics")
diag = diagnostics["params"]["diagnostics"][0]
assert diag["source"] == "dudu/sema"
assert "return type mismatch" in diag["message"]

missing_native_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics"
    and item["params"]["uri"] == missing_native_uri
)
missing_native_diag = missing_native_diagnostics["params"]["diagnostics"][0]
assert missing_native_diag["source"] == "dudu/native-header"
assert "could not scan native header" in missing_native_diag["message"]
assert "hint: add the header directory" in missing_native_diag["message"]

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

member_completion = next(item for item in responses if item.get("id") == 21)
member_completion_labels = [item["label"] for item in member_completion["result"]]
assert "hp" in member_completion_labels
assert "return" not in member_completion_labels

signature = next(item for item in responses if item.get("id") == 6)
assert "def add(a: i32, b: i32) -> i32" in signature["result"]["signatures"][0]["label"]
assert signature["result"]["activeParameter"] == 1

formatting = next(item for item in responses if item.get("id") == 7)
assert "def main() -> i32:\n    value: i32 = add(1, 2)\n    player: Player = Player(3)\n    player.hp\n    return True\n" in formatting["result"][0]["newText"]

references = next(item for item in responses if item.get("id") == 13)
reference_starts = {
    (item["range"]["start"]["line"], item["range"]["start"]["character"])
    for item in references["result"]
}
assert (3, 4) in reference_starts
assert (7, 17) in reference_starts

rename = next(item for item in responses if item.get("id") == 16)
rename_edits = rename["result"]["changes"][uri]
rename_starts = {
    (item["range"]["start"]["line"], item["range"]["start"]["character"], item["newText"])
    for item in rename_edits
}
assert (3, 4, "sum_values") in rename_starts
assert (7, 17, "sum_values") in rename_starts

code_actions = next(item for item in responses if item.get("id") == 17)
assert code_actions["result"][0]["title"] == "Format document"
assert code_actions["result"][0]["kind"] == "source.format"
assert code_actions["result"][0]["command"]["command"] == "editor.action.formatDocument"

workspace_symbols = next(item for item in responses if item.get("id") == 14)
workspace_symbol_names = [item["name"] for item in workspace_symbols["result"]]
assert "add" in workspace_symbol_names

native_symbols = next(item for item in responses if item.get("id") == 8)
native_symbol_names = [item["name"] for item in native_symbols["result"]]
assert "dudu_native.dudu_native_add" in native_symbol_names
assert "dudu_native.DUDU_NATIVE_MAGIC" in native_symbol_names
assert "dudu_native.DUDU_NATIVE_SCALE" in native_symbol_names

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

native_macro_hover = next(item for item in responses if item.get("id") == 15)
assert "macro dudu_native.DUDU_NATIVE_SCALE(arg0)" in native_macro_hover["result"]["contents"]["value"]

workspace_references = next(item for item in responses if item.get("id") == 18)
workspace_reference_uris = {item["uri"] for item in workspace_references["result"]}
assert native_uri in workspace_reference_uris
assert any(uri.endswith("/tests/fixtures/lsp_workspace_helper.dd") for uri in workspace_reference_uris)

workspace_helper_symbols = next(item for item in responses if item.get("id") == 19)
workspace_helper_names = [item["name"] for item in workspace_helper_symbols["result"]]
assert "workspace_helper" in workspace_helper_names
assert any(
    item["location"]["uri"].endswith("/tests/fixtures/lsp_workspace_helper.dd")
    for item in workspace_helper_symbols["result"]
)

shutdown = next(item for item in responses if item.get("id") == 20)
assert shutdown["result"] is None
PY

echo "lsp smoke checks passed"
