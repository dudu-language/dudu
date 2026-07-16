#!/usr/bin/env python3

import json
import subprocess
import sys
from pathlib import Path


def message(value):
    body = json.dumps(value, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}"


def read_messages(data):
    messages = []
    cursor = 0
    while cursor < len(data):
        header_end = data.find(b"\r\n\r\n", cursor)
        if header_end < 0:
            break
        length = None
        for line in data[cursor:header_end].decode().split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
        if length is None:
            raise AssertionError("LSP response omitted Content-Length")
        body_start = header_end + 4
        body_end = body_start + length
        messages.append(json.loads(data[body_start:body_end]))
        cursor = body_end
    return messages


def request(request_id, method, params):
    return message(
        {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}
    )


def notification(method, params):
    return message({"jsonrpc": "2.0", "method": method, "params": params})


repo_root = sys.argv[1]
uri = "file:///tmp/dudu_lsp_recovery.dd"
native_uri = "file:///tmp/dudu_lsp_recovery_native.dd"
Path("/tmp/dudu_lsp_recovery_native.h").write_text(
    "int recovery_native_add(int left, int right);\n"
)
good_source = "\n".join(
    [
        "class Player:",
        "    hp: i32",
        "",
        "def usable(value: i32) -> i32:",
        "    return value + 1",
        "",
        "def main() -> i32:",
        "    result = usable(1)",
        "    return result",
        "",
    ]
)
broken_source = "\n".join(
    [
        "not a declaration",
        "",
        "class Player:",
        "    hp: i32",
        "",
        "def usable(value: i32) -> i32:",
        "    return value + 1",
        "",
        "def damaged() -> i32:",
        "    broken = call(",
        "    return missing_after_broken",
        "",
        "def main() -> i32:",
        "    result = usable(1)",
        "    usa",
        "    return result",
        "",
    ]
)
native_broken_source = "\n".join(
    [
        "from c.path import dudu_lsp_recovery_native.h",
        "not a declaration",
        "",
        "def main() -> i32:",
        "    return recovery_native_add(20, 22)",
        "",
    ]
)

messages = [request(1, "initialize", {})]
messages.append(
    notification(
        "textDocument/didOpen",
        {
            "textDocument": {
                "uri": uri,
                "languageId": "dudu",
                "version": 1,
                "text": good_source,
            }
        },
    )
)
messages.append(
    notification(
        "textDocument/didChange",
        {
            "textDocument": {"uri": uri, "version": 2},
            "contentChanges": [{"text": broken_source}],
        },
    )
)
messages.extend(
    [
        request(
            2,
            "textDocument/hover",
            {"textDocument": {"uri": uri}, "position": {"line": 13, "character": 17}},
        ),
        request(
            3,
            "textDocument/definition",
            {"textDocument": {"uri": uri}, "position": {"line": 13, "character": 17}},
        ),
        request(
            4,
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 14, "character": 7}},
        ),
        request(
            5,
            "textDocument/references",
            {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 5}},
        ),
        request(
            6,
            "textDocument/inlayHint",
            {
                "textDocument": {"uri": uri},
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 17, "character": 0},
                },
            },
        ),
        request(
            7,
            "textDocument/semanticTokens/full",
            {"textDocument": {"uri": uri}},
        ),
        request(
            8,
            "textDocument/documentSymbol",
            {"textDocument": {"uri": uri}},
        ),
    ]
)
messages.append(
    notification(
        "textDocument/didChange",
        {
            "textDocument": {"uri": uri, "version": 3},
            "contentChanges": [{"text": good_source}],
        },
    )
)
messages.append(
    request(
        9,
        "textDocument/hover",
        {"textDocument": {"uri": uri}, "position": {"line": 7, "character": 17}},
    )
)
messages.append(
    notification(
        "textDocument/didOpen",
        {
            "textDocument": {
                "uri": native_uri,
                "languageId": "dudu",
                "version": 1,
                "text": native_broken_source,
            }
        },
    )
)
messages.append(
    request(
        11,
        "textDocument/semanticTokens/full",
        {"textDocument": {"uri": native_uri}},
    )
)
messages.extend(
    [
        request(10, "shutdown", None),
        notification("exit", None),
    ]
)

process = subprocess.run(
    [f"{repo_root}/build/dudu-lsp"],
    input="".join(messages).encode(),
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    timeout=10,
    check=True,
)
if process.stderr:
    raise AssertionError(process.stderr.decode())
responses = read_messages(process.stdout)


def response(request_id):
    return next(item for item in responses if item.get("id") == request_id)


diagnostic_batches = [
    item["params"]["diagnostics"]
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics"
    and item["params"]["uri"] == uri
]
assert len(diagnostic_batches) >= 3, diagnostic_batches
assert diagnostic_batches[0] == [], diagnostic_batches
broken_diagnostics = next(
    batch
    for batch in diagnostic_batches
    if any(item.get("source") == "dudu/parser" for item in batch)
)
broken_notification_index = next(
    index
    for index, item in enumerate(responses)
    if item.get("method") == "textDocument/publishDiagnostics"
    and item["params"]["uri"] == uri
    and any(
        diagnostic.get("source") == "dudu/parser"
        for diagnostic in item["params"]["diagnostics"]
    )
)
first_editor_response_index = next(
    index for index, item in enumerate(responses) if item.get("id") == 2
)
assert broken_notification_index < first_editor_response_index, responses
assert not any(
    "missing_after_broken" in item.get("message", "") for item in broken_diagnostics
)
assert diagnostic_batches[-1] == [], diagnostic_batches[-1]

hover = response(2)["result"]["contents"]["value"]
assert "def usable(value: i32) -> i32" in hover, hover

definition = response(3)["result"]
definition_items = definition if isinstance(definition, list) else [definition]
assert any(item["range"]["start"]["line"] == 5 for item in definition_items), definition

completion_labels = [item["label"] for item in response(4)["result"]]
assert "usable" in completion_labels, completion_labels

references = response(5)["result"]
assert any(item["range"]["start"]["line"] == 13 for item in references), references

inlay_text = json.dumps(response(6)["result"])
assert "i32" in inlay_text, inlay_text
assert response(7)["result"]["data"], response(7)

symbol_names = [item["name"] for item in response(8)["result"]]
assert "Player" in symbol_names, symbol_names
assert "usable" in symbol_names, symbol_names
assert "main" in symbol_names, symbol_names

fixed_hover = response(9)["result"]["contents"]["value"]
assert "def usable(value: i32) -> i32" in fixed_hover, fixed_hover

initialize = response(1)
legend = initialize["result"]["capabilities"]["semanticTokensProvider"]["legend"][
    "tokenTypes"
]
native_data = response(11)["result"]["data"]
decoded = []
line = 0
character = 0
for offset in range(0, len(native_data), 5):
    delta_line, delta_start, length, token_type, modifiers = native_data[offset : offset + 5]
    line += delta_line
    character = character + delta_start if delta_line == 0 else delta_start
    decoded.append((line, character, length, legend[token_type], modifiers))
assert any(
    token[:4] == (4, 11, 19, "function") and token[4] in (16, 32)
    for token in decoded
), decoded
assert any(
    item.get("method") == "workspace/semanticTokens/refresh" for item in responses
), responses

assert response(10)["result"] is None
print("lsp invalid-edit recovery checks passed")
