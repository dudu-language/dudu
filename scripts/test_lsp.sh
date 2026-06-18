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
native_pkg_uri = f"file://{repo_root}/tests/fixtures/lsp_pkg_project/main.dd"
native_pkg_config_uri = f"file://{repo_root}/tests/fixtures/lsp_pkg_project/dudu.toml"
rename_uri = f"file://{repo_root}/tests/fixtures/lsp_rename_main.dd"
rename_user_uri = f"file://{repo_root}/tests/fixtures/lsp_rename_user.dd"
rename_ast_uri = "file:///tmp/dudu_lsp_rename_ast.dd"
lint_uri = "file:///tmp/dudu_lsp_lint.dd"
unused_uri = "file:///tmp/dudu_lsp_unused.dd"
shadow_uri = "file:///tmp/dudu_lsp_shadow.dd"
hover_locals_uri = "file:///tmp/dudu_lsp_hover_locals.dd"
hover_ast_locals_uri = "file:///tmp/dudu_lsp_hover_ast_locals.dd"
hover_docs_uri = "file:///tmp/dudu_lsp_hover_docs.dd"
hazard_uri = "file:///tmp/dudu_lsp_hazards.dd"
unknown_identifier_uri = "file:///tmp/dudu_lsp_unknown_identifier.dd"
import_graph_uri = f"file://{repo_root}/tests/fixtures/lsp_import_graph_entry.dd"
bad_config_uri = f"file://{repo_root}/tests/fixtures/lsp_bad_config/main.dd"
missing_pkg_uri = f"file://{repo_root}/tests/fixtures/lsp_missing_pkg/main.dd"
overload_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_overload.dd"
scope_uri = "file:///tmp/dudu_lsp_scope.dd"
direct_native_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_direct_native.dd"
from_import_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_from_import.dd"
lsp_include_uri = f"file://{repo_root}/tests/fixtures/lsp_include_project/src/main.dd"
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
        "import lsp_workspace_helper as helper",
        'import cpp "native_headers/simple_cpp.hpp" as native_cpp',
        'import c "native_headers/simple_c.h" as dudu_native',
        "",
        "def main() -> i32:",
        "    widget: native_cpp.Widget = native_cpp.Widget(3)",
        "    widget.scaled(2)",
        "    helper.workspace_helper()",
        "    scaled: i32 = dudu_native.DUDU_NATIVE_SCALE(1)",
        "    return scaled + dudu_native.dudu_native_add(20, 22)",
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
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": rename_ast_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def unique_ref_target() -> i32:",
                            "    return 1",
                            "",
                            "def main() -> i32:",
                            "    text = \"unique_ref_target\"",
                            "    # unique_ref_target in a comment",
                            "    return unique_ref_target()",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 54,
            "method": "textDocument/references",
            "params": {
                "textDocument": {"uri": rename_ast_uri},
                "position": {"line": 6, "character": 15},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 55,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {"uri": rename_ast_uri},
                "position": {"line": 0, "character": 5},
                "newName": "unique_ref_renamed",
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 56,
            "method": "textDocument/references",
            "params": {
                "textDocument": {"uri": rename_ast_uri},
                "position": {"line": 4, "character": 15},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 57,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {"uri": rename_ast_uri},
                "position": {"line": 5, "character": 8},
                "newName": "comment_renamed",
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 58,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": rename_ast_uri},
                "position": {"line": 5, "character": 8},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": unknown_identifier_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main() -> i32:",
                            "    return missing_helper",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": lsp_include_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            'import c "lsp_project_header.h"',
                            "",
                            "def main() -> i32:",
                            "    return project_header_value()",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 47,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": lsp_include_uri},
                "position": {"line": 0, "character": 14},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": import_graph_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "import vendor.lsp_import_graph_helper as vendored",
                            "import vendor.lsp_import_graph_helper",
                            "",
                            "def main() -> i32:",
                            "    value: i32 = vendor.lsp_import_graph_helper.vendored_helper()",
                            "    return vendored.vendored_helper() + value",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 42,
            "method": "workspace/symbol",
            "params": {"query": "vendored_helper"},
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 48,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": import_graph_uri},
                "position": {"line": 5, "character": 22},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 49,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": import_graph_uri},
                "position": {"line": 4, "character": 52},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 50,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": import_graph_uri},
                "position": {"line": 4, "character": 52},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 51,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": import_graph_uri},
                "position": {"line": 4, "character": 48},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": hazard_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main() -> i32:",
                            "    wide: i64 = 42",
                            "    small: i32 = i32(wide)",
                            '    small += cpp("1")',
                            '    cpp("asm volatile(\\\"pause\\\" ::: \\\"memory\\\");")',
                            "    return small",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": hover_docs_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "# Adds two numbers.",
                            "# Used by hover docs.",
                            "def documented_add(a: i32, b: i32) -> i32:",
                            "    return a + b",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 41,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": hover_docs_uri},
                "position": {"line": 2, "character": 6},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": hover_locals_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main(amount: i32) -> i32:",
                            "    inferred = 42",
                            "    explicit: f32 = 2.0",
                            "    hex_value = 0x2A",
                            "    inferred",
                            "    hex_value",
                            "    return amount + inferred + hex_value",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 39,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": hover_locals_uri},
                "position": {"line": 4, "character": 6},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 40,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": hover_locals_uri},
                "position": {"line": 2, "character": 6},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 45,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": hover_locals_uri},
                "position": {"line": 5, "character": 6},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": hover_ast_locals_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def add(a: i32, b: i32) -> i32:",
                            "    return a + b",
                            "",
                            "def main(",
                            "    amount: i32,",
                            "    extra: i32,",
                            ") -> i32:",
                            "    total = add(amount, extra)",
                            "    total",
                            "    extra",
                            "    return total",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 52,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": hover_ast_locals_uri},
                "position": {"line": 8, "character": 6},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 53,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": hover_ast_locals_uri},
                "position": {"line": 9, "character": 6},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": from_import_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "from lsp_workspace_helper import workspace_helper as direct_helper",
                            "",
                            "def main() -> i32:",
                            "    return direct_helper()",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 38,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": from_import_uri},
                "position": {"line": 3, "character": 12},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": direct_native_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            'import c "native_headers/simple_c.h"',
                            "",
                            "def main() -> i32:",
                            "    event: DuduNativeEvent",
                            "    return dudu_native_add(20, 22)",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 35,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": direct_native_uri},
                "position": {"line": 4, "character": 11},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 36,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {"uri": direct_native_uri},
                "position": {"line": 4, "character": 30},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 37,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": direct_native_uri},
                "position": {"line": 4, "character": 14},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 43,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": direct_native_uri},
                "position": {"line": 0, "character": 18},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": unused_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main() -> i32:",
                            "    unused_value: i32 = 1",
                            "    used_value: i32 = 2",
                            "    return used_value",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 34,
            "method": "textDocument/codeAction",
            "params": {
                "textDocument": {"uri": unused_uri},
                "range": {
                    "start": {"line": 1, "character": 4},
                    "end": {"line": 1, "character": 22},
                },
                "context": {
                    "diagnostics": [
                        {
                            "range": {
                                "start": {"line": 1, "character": 4},
                                "end": {"line": 1, "character": 5},
                            },
                            "source": "dudu/lint",
                            "code": "dudu.lint.unused",
                            "message": "unused local: unused_value",
                        }
                    ]
                },
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": shadow_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main(a: i32) -> i32:",
                            "    value: i32 = 1",
                            "    if a > 0:",
                            "        value: i32 = 2",
                            "        return value",
                            "    return value",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": scope_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main() -> i32:",
                            "    outer_value: i32 = 1",
                            "    if True:",
                            "        inner_only: i32 = 2",
                            "    return outer_value",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 33,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": scope_uri},
                "position": {"line": 4, "character": 11},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": overload_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            'import cpp "native_headers/simple_cpp.hpp" as native_cpp',
                            "",
                            "def main() -> i32:",
                            "    amount: f32 = 2.0",
                            "    return i32(native_cpp.dudu_native.overloaded(amount))",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": missing_pkg_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main() -> i32:",
                            "    return 0",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 32,
            "method": "textDocument/signatureHelp",
            "params": {
                "textDocument": {"uri": overload_uri},
                "position": {"line": 4, "character": 51},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": bad_config_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            'import c "stdio.h"',
                            "",
                            "def main() -> i32:",
                            "    return 0",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 46,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": bad_config_uri},
                "position": {"line": 0, "character": 11},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": lint_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main() -> i32:",
                            "    return 0",
                            "    value: i32 = 1",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 29,
            "method": "textDocument/codeAction",
            "params": {
                "textDocument": {"uri": lint_uri},
                "range": {
                    "start": {"line": 2, "character": 4},
                    "end": {"line": 2, "character": 18},
                },
                "context": {
                    "diagnostics": [
                        {
                            "range": {
                                "start": {"line": 2, "character": 4},
                                "end": {"line": 2, "character": 5},
                            },
                            "source": "dudu/lint",
                            "code": "dudu.lint.unreachable",
                            "message": "unreachable statement after terminating statement",
                        }
                    ]
                },
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": rename_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def rename_target() -> i32:",
                            "    return 1",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 28,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {"uri": rename_uri},
                "position": {"line": 0, "character": 5},
                "newName": "renamed_target",
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
            "id": 44,
            "method": "textDocument/semanticTokens/full",
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
            "id": 24,
            "method": "completionItem/resolve",
            "params": {
                "label": "def",
                "kind": 15,
                "detail": "snippet",
                "insertText": "def ${1:name}(${2:args}) -> ${3:i32}:\n    ${0:return 0}",
                "insertTextFormat": 2,
            },
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
                "position": {"line": 9, "character": 45},
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
                "position": {"line": 9, "character": 45},
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
                "position": {"line": 9, "character": 11},
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
                "position": {"line": 9, "character": 53},
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
                "position": {"line": 8, "character": 35},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 22,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 6, "character": 11},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 23,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 7, "character": 11},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 30,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 7, "character": 6},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 31,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 6, "character": 13},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 25,
            "method": "textDocument/codeAction",
            "params": {
                "textDocument": {"uri": native_uri},
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 0},
                },
                "context": {"diagnostics": []},
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 26,
            "method": "textDocument/codeAction",
            "params": {
                "textDocument": {"uri": native_uri},
                "range": {
                    "start": {"line": 8, "character": 11},
                    "end": {"line": 8, "character": 25},
                },
                "context": {
                    "diagnostics": [
                        {
                            "range": {
                                "start": {"line": 8, "character": 11},
                                "end": {"line": 8, "character": 25},
                            },
                            "source": "dudu/sema",
                            "code": "dudu.sema.unknown_identifier",
                            "data": {"name": "missing_helper"},
                            "message": "unknown identifier: missing_helper",
                        }
                    ]
                },
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
                "position": {"line": 7, "character": 18},
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
    packet(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": native_pkg_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            'import c "raylib.h"',
                            "",
                            "def main() -> i32:",
                            "    return 0",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
    packet(
        {
            "jsonrpc": "2.0",
            "id": 27,
            "method": "textDocument/codeAction",
            "params": {
                "textDocument": {"uri": native_pkg_uri},
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 18},
                },
                "context": {
                    "diagnostics": [
                        {
                            "range": {
                                "start": {"line": 0, "character": 0},
                                "end": {"line": 0, "character": 18},
                            },
                            "source": "dudu/native-header",
                            "message": "could not scan native header raylib.h\nfatal error: 'raylib.h' file not found",
                        }
                    ]
                },
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
assert initialize["result"]["capabilities"]["completionProvider"]["resolveProvider"] is True
assert initialize["result"]["capabilities"]["workspaceSymbolProvider"] is True
semantic_provider = initialize["result"]["capabilities"]["semanticTokensProvider"]
assert semantic_provider["full"] is True
assert "class" in semantic_provider["legend"]["tokenTypes"]

diagnostics = next(item for item in responses if item.get("method") == "textDocument/publishDiagnostics")
diag = diagnostics["params"]["diagnostics"][0]
assert diag["source"] == "dudu/sema"
assert "return type mismatch" in diag["message"]

unknown_identifier_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics"
    and item["params"]["uri"] == unknown_identifier_uri
)
missing_helper_diag = next(
    item
    for item in unknown_identifier_diagnostics["params"]["diagnostics"]
    if item.get("code") == "dudu.sema.unknown_identifier"
    and item.get("data", {}).get("name") == "missing_helper"
)
assert missing_helper_diag["code"] == "dudu.sema.unknown_identifier"
assert missing_helper_diag["data"]["name"] == "missing_helper"

missing_native_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics"
    and item["params"]["uri"] == missing_native_uri
)
missing_native_diag = missing_native_diagnostics["params"]["diagnostics"][0]
assert missing_native_diag["source"] == "dudu/native-header"
assert missing_native_diag["code"] == "dudu.native_header.scan_failed"
assert missing_native_diag["data"]["name"] == "./native_headers/does_not_exist.h"
assert "could not scan native header" in missing_native_diag["message"]
assert "hint: add the header directory" in missing_native_diag["message"]

lint_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics" and item["params"]["uri"] == lint_uri
)
lint_diag = next(
    item
    for item in lint_diagnostics["params"]["diagnostics"]
    if item["message"] == "unreachable statement after terminating statement"
)
assert lint_diag["source"] == "dudu/lint"
assert lint_diag["severity"] == 2
assert lint_diag["code"] == "dudu.lint.unreachable"
assert lint_diag["range"]["start"]["line"] == 2

unused_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics" and item["params"]["uri"] == unused_uri
)
unused_messages = [item["message"] for item in unused_diagnostics["params"]["diagnostics"]]
assert "unused local: unused_value" in unused_messages
assert "unused local: used_value" not in unused_messages
unused_codes = {
    item["message"]: item.get("code") for item in unused_diagnostics["params"]["diagnostics"]
}
assert unused_codes["unused local: unused_value"] == "dudu.lint.unused"

shadow_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics" and item["params"]["uri"] == shadow_uri
)
shadow_messages = [item["message"] for item in shadow_diagnostics["params"]["diagnostics"]]
assert "local shadows outer binding: value" in shadow_messages
shadow_codes = {
    item["message"]: item.get("code") for item in shadow_diagnostics["params"]["diagnostics"]
}
assert shadow_codes["local shadows outer binding: value"] == "dudu.lint.shadow"

hazard_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics" and item["params"]["uri"] == hazard_uri
)
hazard_messages = [item["message"] for item in hazard_diagnostics["params"]["diagnostics"]]
assert "suspicious narrowing cast: i32(wide) from i64" in hazard_messages
assert "native interop hazard: raw cpp escape hatch" in hazard_messages
hazard_codes = {
    item["message"]: item.get("code") for item in hazard_diagnostics["params"]["diagnostics"]
}
assert hazard_codes["suspicious narrowing cast: i32(wide) from i64"] == "dudu.lint.suspicious_cast"
assert hazard_codes["native interop hazard: raw cpp escape hatch"] == "dudu.lint.cpp_escape"

build_config_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics"
    and item["params"]["uri"] == bad_config_uri
)
build_config_diag = build_config_diagnostics["params"]["diagnostics"][0]
assert build_config_diag["source"] == "dudu/build-config", build_config_diag
assert "invalid [target] kind" in build_config_diag["message"], build_config_diag

bad_config_definition = next(item for item in responses if item.get("id") == 46)
assert "error" in bad_config_definition, bad_config_definition
assert bad_config_definition["error"]["code"] == -32603, bad_config_definition
assert "invalid [target] kind" in bad_config_definition["error"]["message"], bad_config_definition

missing_pkg_diagnostics = next(
    item
    for item in responses
    if item.get("method") == "textDocument/publishDiagnostics"
    and item["params"]["uri"] == missing_pkg_uri
)
missing_pkg_diag = missing_pkg_diagnostics["params"]["diagnostics"][0]
assert missing_pkg_diag["source"] == "dudu/build-config"
assert "missing pkg-config package: definitely_missing_dudu_pkg_config_package" in missing_pkg_diag["message"]

symbols = next(item for item in responses if item.get("id") == 2)
symbol_names = [item["name"] for item in symbols["result"]]
assert "Player" in symbol_names
assert "add" in symbol_names
assert "main" in symbol_names

hover = next(item for item in responses if item.get("id") == 3)
assert "def add" in hover["result"]["contents"]["value"]

inferred_hover = next(item for item in responses if item.get("id") == 39)
assert "inferred: i32" in inferred_hover["result"]["contents"]["value"]

typed_hover = next(item for item in responses if item.get("id") == 40)
assert "explicit: f32" in typed_hover["result"]["contents"]["value"]

hex_hover = next(item for item in responses if item.get("id") == 45)
assert "hex_value: i32" in hex_hover["result"]["contents"]["value"]

ast_inferred_hover = next(item for item in responses if item.get("id") == 52)
assert "total: i32" in ast_inferred_hover["result"]["contents"]["value"]

ast_param_hover = next(item for item in responses if item.get("id") == 53)
assert "extra: i32" in ast_param_hover["result"]["contents"]["value"]

doc_hover = next(item for item in responses if item.get("id") == 41)
doc_hover_value = doc_hover["result"]["contents"]["value"]
assert "def documented_add" in doc_hover_value
assert "Adds two numbers." in doc_hover_value
assert "Used by hover docs." in doc_hover_value

imported_hover = next(item for item in responses if item.get("id") == 48)
assert "def vendored_helper" in imported_hover["result"]["contents"]["value"]

nested_import_hover = next(item for item in responses if item.get("id") == 49)
assert "def vendored_helper" in nested_import_hover["result"]["contents"]["value"]

nested_import_definition = next(item for item in responses if item.get("id") == 50)
assert nested_import_definition["result"]["uri"].endswith(
    "/tests/fixtures/vendor/lsp_import_graph_helper.dd"
)
assert nested_import_definition["result"]["range"]["start"]["line"] == 0

nested_module_completion = next(item for item in responses if item.get("id") == 51)
nested_module_completion_labels = [item["label"] for item in nested_module_completion["result"]]
assert "vendored_helper" in nested_module_completion_labels

definition = next(item for item in responses if item.get("id") == 4)
assert definition["result"]["range"]["start"]["line"] == 3

semantic_tokens = next(item for item in responses if item.get("id") == 44)
semantic_data = semantic_tokens["result"]["data"]
assert semantic_data
legend = semantic_provider["legend"]["tokenTypes"]
decoded = []
line = 0
character = 0
for i in range(0, len(semantic_data), 5):
    delta_line, delta_start, length, token_type, modifiers = semantic_data[i : i + 5]
    line += delta_line
    character = character + delta_start if delta_line == 0 else delta_start
    decoded.append((line, character, length, legend[token_type], modifiers))
assert (0, 6, 6, "class", 1) in decoded
assert (1, 4, 2, "property", 1) in decoded
assert (3, 4, 3, "function", 1) in decoded
assert any(item[3] == "parameter" and item[2] == 1 for item in decoded)
assert any(item[3] == "type" and item[2] == 3 for item in decoded)
assert any(item[3] == "variable" and item[2] == 5 for item in decoded)

completion = next(item for item in responses if item.get("id") == 5)
completion_labels = [item["label"] for item in completion["result"]]
assert "return" in completion_labels
assert "i32" in completion_labels
assert "add" in completion_labels
assert "Player" in completion_labels
assert "value" in completion_labels
assert "player" in completion_labels
assert any(
    item["label"] == "def" and item.get("insertTextFormat") == 2 and "${1:name}" in item.get("insertText", "")
    for item in completion["result"]
)
for snippet_label in ["while", "enum", "import", "from", "except"]:
    assert any(
        item["label"] == snippet_label
        and item.get("insertTextFormat") == 2
        and "${" in item.get("insertText", "")
        for item in completion["result"]
    )

resolved_completion = next(item for item in responses if item.get("id") == 24)
assert resolved_completion["result"]["documentation"]["kind"] == "markdown"
assert "Dudu snippet" in resolved_completion["result"]["documentation"]["value"]

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

ast_references = next(item for item in responses if item.get("id") == 54)
ast_reference_starts = {
    (item["range"]["start"]["line"], item["range"]["start"]["character"])
    for item in ast_references["result"]
    if item["uri"] == rename_ast_uri
}
assert ast_reference_starts == {(0, 4), (6, 11)}

ast_rename = next(item for item in responses if item.get("id") == 55)
ast_rename_edits = ast_rename["result"]["changes"][rename_ast_uri]
ast_rename_starts = {
    (item["range"]["start"]["line"], item["range"]["start"]["character"], item["newText"])
    for item in ast_rename_edits
}
assert ast_rename_starts == {
    (0, 4, "unique_ref_renamed"),
    (6, 11, "unique_ref_renamed"),
}

ast_string_references = next(item for item in responses if item.get("id") == 56)
assert ast_string_references["result"] == []

ast_comment_rename = next(item for item in responses if item.get("id") == 57)
assert ast_comment_rename["result"] is None

ast_comment_definition = next(item for item in responses if item.get("id") == 58)
assert ast_comment_definition["result"] is None

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
assert "dudu_native.dudu_native_kind_ok" in native_symbol_names

native_hover = next(item for item in responses if item.get("id") == 9)
assert "dudu_native.dudu_native_add(i32, i32) -> i32" in native_hover["result"]["contents"]["value"]

native_definition = next(item for item in responses if item.get("id") == 10)
assert native_definition["result"]["uri"].endswith("/tests/fixtures/native_headers/simple_c.h")
assert native_definition["result"]["range"]["start"]["line"] == 20

native_completion = next(item for item in responses if item.get("id") == 11)
native_completion_labels = [item["label"] for item in native_completion["result"]]
assert "dudu_native.dudu_native_add" in native_completion_labels
assert "dudu_native.dudu_native_kind_ok" in native_completion_labels

direct_native_completion = next(item for item in responses if item.get("id") == 35)
direct_native_labels = [item["label"] for item in direct_native_completion["result"]]
assert "dudu_native_add" in direct_native_labels
assert "DUDU_NATIVE_MAGIC" in direct_native_labels
assert "dudu_native_kind_ok" in direct_native_labels

native_signature = next(item for item in responses if item.get("id") == 12)
assert "dudu_native.dudu_native_add(i32, i32) -> i32" in native_signature["result"]["signatures"][0]["label"]
assert native_signature["result"]["activeParameter"] == 1

direct_native_signature = next(item for item in responses if item.get("id") == 36)
assert "dudu_native_add(i32, i32) -> i32" in direct_native_signature["result"]["signatures"][0]["label"]
assert direct_native_signature["result"]["activeParameter"] == 1

direct_native_definition = next(item for item in responses if item.get("id") == 37)
assert direct_native_definition["result"]["uri"].endswith("/tests/fixtures/native_headers/simple_c.h")
assert direct_native_definition["result"]["range"]["start"]["line"] == 20

direct_native_header_definition = next(item for item in responses if item.get("id") == 43)
assert direct_native_header_definition["result"]["uri"].endswith("/tests/fixtures/native_headers/simple_c.h")
assert direct_native_header_definition["result"]["range"]["start"]["line"] == 0

lsp_include_header_definition = next(item for item in responses if item.get("id") == 47)
assert lsp_include_header_definition["result"]["uri"].endswith(
    "/tests/fixtures/lsp_include_project/include/lsp_project_header.h"
)
assert lsp_include_header_definition["result"]["range"]["start"]["line"] == 0

overload_signature = next(item for item in responses if item.get("id") == 32)
overload_labels = [item["label"] for item in overload_signature["result"]["signatures"]]
assert "native_cpp.dudu_native.overloaded(i32) -> i32" in overload_labels
assert "native_cpp.dudu_native.overloaded(f32) -> f32" in overload_labels

scope_completion = next(item for item in responses if item.get("id") == 33)
scope_labels = [item["label"] for item in scope_completion["result"]]
assert "outer_value" in scope_labels
assert "inner_only" not in scope_labels

native_macro_hover = next(item for item in responses if item.get("id") == 15)
assert "macro dudu_native.DUDU_NATIVE_SCALE(arg0)" in native_macro_hover["result"]["contents"]["value"]

native_member_completion = next(item for item in responses if item.get("id") == 22)
native_member_labels = [item["label"] for item in native_member_completion["result"]]
assert "value" in native_member_labels
assert "scaled" in native_member_labels
assert "return" not in native_member_labels

module_completion = next(item for item in responses if item.get("id") == 23)
module_completion_labels = [item["label"] for item in module_completion["result"]]
assert "workspace_helper" in module_completion_labels
assert "return" not in module_completion_labels

import_definition = next(item for item in responses if item.get("id") == 30)
assert import_definition["result"]["uri"].endswith("/tests/fixtures/lsp_workspace_helper.dd")
assert import_definition["result"]["range"]["start"]["line"] == 0

from_import_definition = next(item for item in responses if item.get("id") == 38)
assert from_import_definition["result"]["uri"].endswith("/tests/fixtures/lsp_workspace_helper.dd")
assert from_import_definition["result"]["range"]["start"]["line"] == 0

native_member_definition = next(item for item in responses if item.get("id") == 31)
assert native_member_definition["result"]["uri"].endswith("/tests/fixtures/native_headers/simple_cpp.hpp")
assert native_member_definition["result"]["range"]["start"]["line"] == 9

native_code_actions = next(item for item in responses if item.get("id") == 25)
organize_imports = next(
    item for item in native_code_actions["result"] if item["title"] == "Organize imports"
)
organize_edit = organize_imports["edit"]["changes"][native_uri][0]
assert organize_edit["range"]["start"]["line"] == 0
assert organize_edit["range"]["end"]["line"] == 3
assert organize_edit["newText"].splitlines() == [
    'import c "native_headers/simple_c.h" as dudu_native',
    'import cpp "native_headers/simple_cpp.hpp" as native_cpp',
    "import lsp_workspace_helper as helper",
]

missing_import_actions = next(item for item in responses if item.get("id") == 26)
missing_import = next(
    item
    for item in missing_import_actions["result"]
    if item["title"] == "Import missing_helper from lsp_import_target"
)
missing_import_edit = missing_import["edit"]["changes"][native_uri][0]
assert missing_import["kind"] == "quickfix"
assert missing_import_edit["range"]["start"]["line"] == 3
assert missing_import_edit["range"]["end"]["line"] == 3
assert missing_import_edit["newText"] == "from lsp_import_target import missing_helper\n"

native_config_actions = next(item for item in responses if item.get("id") == 27)
assert not any(
    item["title"].startswith("Add pkg-config package ")
    for item in native_config_actions["result"]
)

workspace_rename = next(item for item in responses if item.get("id") == 28)
assert rename_uri in workspace_rename["result"]["changes"]
assert rename_user_uri in workspace_rename["result"]["changes"]
workspace_rename_user_edits = workspace_rename["result"]["changes"][rename_user_uri]
assert any(
    edit["range"]["start"]["line"] == 1
    and edit["range"]["start"]["character"] == 11
    and edit["newText"] == "renamed_target"
    for edit in workspace_rename_user_edits
)

lint_actions = next(item for item in responses if item.get("id") == 29)
lint_fix = next(
    item for item in lint_actions["result"] if item["title"] == "Remove unreachable statement"
)
lint_edit = lint_fix["edit"]["changes"][lint_uri][0]
assert lint_fix["kind"] == "quickfix"
assert lint_edit["range"]["start"]["line"] == 2
assert lint_edit["range"]["end"]["line"] == 3
assert lint_edit["newText"] == ""

unused_actions = next(item for item in responses if item.get("id") == 34)
unused_fix = next(item for item in unused_actions["result"] if item["title"] == "Remove unused local")
unused_edit = unused_fix["edit"]["changes"][unused_uri][0]
assert unused_fix["kind"] == "quickfix"
assert unused_edit["range"]["start"]["line"] == 1
assert unused_edit["range"]["start"]["character"] == 0
assert unused_edit["range"]["end"]["line"] == 2
assert unused_edit["range"]["end"]["character"] == 0
assert unused_edit["newText"] == ""

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

import_graph_symbols = next(item for item in responses if item.get("id") == 42)
assert any(
    item["name"] == "vendored_helper"
    and item["location"]["uri"].endswith("/tests/fixtures/vendor/lsp_import_graph_helper.dd")
    for item in import_graph_symbols["result"]
)

shutdown = next(item for item in responses if item.get("id") == 20)
assert shutdown["result"] is None
PY

echo "lsp smoke checks passed"
