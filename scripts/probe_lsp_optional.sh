#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/dev_env.sh"
"$repo_root/scripts/build.sh" >/dev/null

python3 - "$repo_root" <<'PY'
import json
import shutil
import subprocess
import sys
from pathlib import Path

repo_root = Path(sys.argv[1])


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


def pkg_exists(name):
    return subprocess.run(["pkg-config", "--exists", name], check=False).returncode == 0


def run_case(case):
    if not pkg_exists(case["pkg"]):
        print(f"skip {case['name']}: pkg-config package not found")
        return

    work = repo_root / "build" / "lsp-optional" / case["name"]
    work.mkdir(parents=True, exist_ok=True)
    (work / "dudu.toml").write_text(
        f'name = "{case["name"]}"\nentry = "main.dd"\n\n[pkg]\nlibs = ["{case["pkg"]}"]\n'
    )
    source = "\n".join(case["source"]) + "\n"
    (work / "main.dd").write_text(source)
    uri = f"file://{work / 'main.dd'}"

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
                "method": "textDocument/completion",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": case["completion_position"],
                },
            }
        ),
        packet(
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "textDocument/signatureHelp",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": case["signature_position"],
                },
            }
        ),
        packet(
            {
                "jsonrpc": "2.0",
                "id": 4,
                "method": "textDocument/definition",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": case["definition_position"],
                },
            }
        ),
        packet(
            {
                "jsonrpc": "2.0",
                "id": 5,
                "method": "textDocument/hover",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": case["hover_position"],
                },
            }
        ),
        packet({"jsonrpc": "2.0", "id": 6, "method": "shutdown", "params": None}),
        packet({"jsonrpc": "2.0", "method": "exit", "params": None}),
    ]
    proc = subprocess.run(
        [str(repo_root / "build" / "duc"), "lsp"],
        input="".join(messages).encode(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=True,
    )
    responses = read_packets(proc.stdout)
    diagnostic_packets = [
        item
        for item in responses
        if item.get("method") == "textDocument/publishDiagnostics"
        and item["params"]["uri"] == uri
    ]
    diagnostics = diagnostic_packets[0]["params"]["diagnostics"] if diagnostic_packets else []
    if diagnostics:
        raise AssertionError(f"{case['name']} diagnostics: {diagnostics}")

    completion = next(item for item in responses if item.get("id") == 2)
    labels = [item["label"] for item in completion["result"]]
    for label in case["completion_labels"]:
        if label not in labels:
            raise AssertionError(f"{case['name']} missing completion {label}")

    signature = next(item for item in responses if item.get("id") == 3)
    signature_labels = [item["label"] for item in signature["result"]["signatures"]]
    if not any(case["signature_contains"] in label for label in signature_labels):
        raise AssertionError(f"{case['name']} signatures: {signature_labels}")

    definition = next(item for item in responses if item.get("id") == 4)
    if definition["result"] is None:
        raise AssertionError(f"{case['name']} missing definition")

    hover = next(item for item in responses if item.get("id") == 5)
    hover_result = hover["result"]
    if hover_result is None:
        raise AssertionError(f"{case['name']} missing hover")
    hover_value = hover_result["contents"]["value"]
    if case["hover_contains"] not in hover_value:
        raise AssertionError(f"{case['name']} hover: {hover_value}")
    print(f"ok {case['name']}")


cases = [
    {
        "name": "sqlite3",
        "pkg": "sqlite3",
        "source": [
            'import c "sqlite3.h"',
            "",
            "def main() -> i32:",
            "    db: *sqlite3 = None",
            '    return sqlite3_open(":memory:", &db)',
        ],
        "completion_position": {"line": 4, "character": 11},
        "completion_labels": ["sqlite3_open", "sqlite3"],
        "signature_position": {"line": 4, "character": 37},
        "signature_contains": "sqlite3_open",
        "definition_position": {"line": 4, "character": 14},
        "hover_position": {"line": 4, "character": 14},
        "hover_contains": "sqlite3_open",
    },
    {
        "name": "raylib",
        "pkg": "raylib",
        "source": [
            'import c "raylib.h"',
            "",
            "def main() -> i32:",
            "    pos: Vector2 = Vector2(1.0, 2.0)",
            '    InitWindow(1, 1, "dudu")',
            "    return i32(pos.x)",
        ],
        "completion_position": {"line": 4, "character": 4},
        "completion_labels": ["InitWindow", "Vector2"],
        "signature_position": {"line": 4, "character": 27},
        "signature_contains": "InitWindow",
        "definition_position": {"line": 3, "character": 10},
        "hover_position": {"line": 3, "character": 10},
        "hover_contains": "Vector2",
    },
    {
        "name": "sdl3",
        "pkg": "sdl3",
        "source": [
            'import c "SDL3/SDL.h"',
            "",
            "def main() -> i32:",
            "    SDL_Init(0)",
            "    return 0",
        ],
        "completion_position": {"line": 3, "character": 4},
        "completion_labels": ["SDL_Init"],
        "signature_position": {"line": 3, "character": 13},
        "signature_contains": "SDL_Init",
        "definition_position": {"line": 3, "character": 6},
        "hover_position": {"line": 3, "character": 6},
        "hover_contains": "SDL_Init",
    },
    {
        "name": "glfw3",
        "pkg": "glfw3",
        "source": [
            'import c "GLFW/glfw3.h"',
            "",
            "def main() -> i32:",
            "    glfwInit()",
            "    return 0",
        ],
        "completion_position": {"line": 3, "character": 4},
        "completion_labels": ["glfwInit"],
        "signature_position": {"line": 3, "character": 13},
        "signature_contains": "glfwInit",
        "definition_position": {"line": 3, "character": 7},
        "hover_position": {"line": 3, "character": 7},
        "hover_contains": "glfwInit",
    },
]


if shutil.which("pkg-config") is None:
    print("skip optional LSP probes: pkg-config not found")
    sys.exit(0)

for case in cases:
    run_case(case)
PY
