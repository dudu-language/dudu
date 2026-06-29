messages.extend([
    lsp_message(
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
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/documentSymbol",
            "params": {"textDocument": {"uri": uri}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 44,
            "method": "textDocument/semanticTokens/full",
            "params": {"textDocument": {"uri": uri}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "textDocument/hover",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 5}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "textDocument/definition",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 5}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 5,
            "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 4}},
        }
    ),
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "textDocument/signatureHelp",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 7, "character": 25}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 21,
            "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 9, "character": 11}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 7,
            "method": "textDocument/formatting",
            "params": {"textDocument": {"uri": uri}, "options": {"tabSize": 4}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 13,
            "method": "textDocument/references",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 7, "character": 18}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 63,
            "method": "textDocument/references",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 5}},
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 61,
            "method": "textDocument/references",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 13}},
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message({"jsonrpc": "2.0", "id": 14, "method": "workspace/symbol", "params": {"query": "add"}}),
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 8,
            "method": "textDocument/documentSymbol",
            "params": {"textDocument": {"uri": native_uri}},
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 69,
            "method": "textDocument/references",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 9, "character": 45},
            },
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 60,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 5, "character": 25},
            },
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 65,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 5, "character": 25},
            },
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 67,
            "method": "textDocument/references",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 5, "character": 25},
            },
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 68,
            "method": "textDocument/references",
            "params": {
                "textDocument": {"uri": native_uri},
                "position": {"line": 5, "character": 45},
            },
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 66,
            "method": "textDocument/semanticTokens/full",
            "params": {"textDocument": {"uri": native_uri}},
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": ambiguous_import_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def main() -> i32:",
                            "    return ambiguous_helper()",
                            "",
                        ]
                    ),
                }
            },
        }
    ),
])
