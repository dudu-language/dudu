#!/usr/bin/env python3

import json
import subprocess
import sys


def encode(value):
    body = json.dumps(value, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}"


def request(request_id, method, params):
    return encode({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})


def notification(method, params):
    return encode({"jsonrpc": "2.0", "method": method, "params": params})


def decode(data):
    messages = []
    cursor = 0
    while cursor < len(data):
        header_end = data.find(b"\r\n\r\n", cursor)
        assert header_end >= 0
        headers = data[cursor:header_end].decode().split("\r\n")
        length = int(
            next(
                line.split(":", 1)[1]
                for line in headers
                if line.lower().startswith("content-length:")
            )
        )
        body_start = header_end + 4
        body_end = body_start + length
        messages.append(json.loads(data[body_start:body_end]))
        cursor = body_end
    return messages


repo = sys.argv[1]
uri = "file:///tmp/dudu_lsp_incremental_sync.dd"
source = "\n".join(
    [
        "def count() -> i32:",
        "    total = 0",
        "    for i in range(3):",
        "        total += 1",
        "    return total",
        "",
    ]
)
document = {"uri": uri, "languageId": "dudu", "version": 1, "text": source}

messages = [request(1, "initialize", {})]
messages.append(notification("textDocument/didOpen", {"textDocument": document}))
messages.append(
    notification(
        "textDocument/didChange",
        {
            "textDocument": {"uri": uri, "version": 2},
            "contentChanges": [
                {
                    "range": {
                        "start": {"line": 2, "character": 8},
                        "end": {"line": 2, "character": 9},
                    },
                    "rangeLength": 1,
                    "text": ":",
                }
            ],
        },
    )
)
messages.append(request(2, "textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}))
messages.append(
    request(
        3,
        "textDocument/inlayHint",
        {
            "textDocument": {"uri": uri},
            "range": {"start": {"line": 0, "character": 0}, "end": {"line": 6, "character": 0}},
        },
    )
)
messages.append(
    notification(
        "textDocument/didChange",
        {
            "textDocument": {"uri": uri, "version": 3},
            "contentChanges": [
                {
                    "range": {
                        "start": {"line": 2, "character": 8},
                        "end": {"line": 2, "character": 9},
                    },
                    "rangeLength": 1,
                    "text": "_",
                }
            ],
        },
    )
)
messages.append(request(4, "textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}))
messages.append(
    request(
        5,
        "textDocument/inlayHint",
        {
            "textDocument": {"uri": uri},
            "range": {"start": {"line": 0, "character": 0}, "end": {"line": 6, "character": 0}},
        },
    )
)
messages.append(
    request(
        6,
        "textDocument/hover",
        {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 12}},
    )
)
messages.append(request(7, "shutdown", None))
messages.append(notification("exit", None))

process = subprocess.run(
    [f"{repo}/build/dudu-lsp"],
    input="".join(messages).encode(),
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    timeout=10,
    check=True,
)
assert not process.stderr, process.stderr.decode()
responses = decode(process.stdout)


def response(request_id):
    return next(item for item in responses if item.get("id") == request_id)


diagnostics = [
    item["params"]["diagnostics"]
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics" and item["params"]["uri"] == uri
]
assert len(diagnostics) == 3, diagnostics
assert any(item.get("source") == "dudu/parser" for item in diagnostics[1]), diagnostics[1]
assert all(item["range"]["start"]["line"] == 2 for item in diagnostics[1]), diagnostics[1]
assert diagnostics[2] == [], diagnostics[2]

assert response(2)["result"]["data"], response(2)
assert "i32" in json.dumps(response(3)["result"]), response(3)
assert response(4)["result"]["data"], response(4)
assert "i32" in json.dumps(response(5)["result"]), response(5)
assert "i32" in response(6)["result"]["contents"]["value"], response(6)
assert response(7)["result"] is None
print("lsp incremental synchronization checks passed")
