messages.extend([
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 64,
            "method": "textDocument/codeAction",
            "params": {
                "textDocument": {"uri": ambiguous_import_uri},
                "range": {
                    "start": {"line": 1, "character": 11},
                    "end": {"line": 1, "character": 27},
                },
                "context": {
                    "diagnostics": [
                        {
                            "range": {
                                "start": {"line": 1, "character": 11},
                                "end": {"line": 1, "character": 27},
                            },
                            "source": "dudu/sema",
                            "code": "dudu.sema.unknown_identifier",
                            "data": {"name": "ambiguous_helper"},
                            "message": "unknown identifier: ambiguous_helper",
                        }
                    ]
                },
            },
        }
    ),
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 19,
            "method": "workspace/symbol",
            "params": {"query": "workspace_helper"},
        }
    ),
    lsp_message(
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
    lsp_message(
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
                            "from c import raylib.h",
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
    lsp_message({"jsonrpc": "2.0", "id": 20, "method": "shutdown", "params": None}),
    lsp_message({"jsonrpc": "2.0", "method": "exit", "params": None}),
])
