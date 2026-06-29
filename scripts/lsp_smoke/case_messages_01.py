messages.extend([
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 70,
            "method": "textDocument/references",
            "params": {
                "textDocument": {"uri": direct_native_uri},
                "position": {"line": 4, "character": 14},
            },
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
                            "data": {
                                "fixRange": {
                                    "start": {"line": 1, "character": 0},
                                    "end": {"line": 2, "character": 0},
                                }
                            },
                        }
                    ]
                },
            },
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
                            "data": {
                                "fixRange": {
                                    "start": {"line": 2, "character": 0},
                                    "end": {"line": 3, "character": 0},
                                }
                            },
                        }
                    ]
                },
            },
        }
    ),
])
