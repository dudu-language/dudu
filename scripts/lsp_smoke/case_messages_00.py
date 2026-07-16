messages.extend([
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {"rootUri": Path(repo_root).resolve().as_uri()},
        }
    ),
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": unrelated_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def add(a: i32, b: i32) -> i32:",
                            "    return a - b",
                            "",
                            "def main() -> i32:",
                            "    return add(8, 3)",
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
                    "uri": rename_unrelated_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def rename_target() -> i32:",
                            "    return 99",
                            "",
                            "def use_local_target() -> i32:",
                            "    return rename_target()",
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
            "id": 59,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": uri},
            },
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": rename_ast_unrelated_uri,
                    "languageId": "dudu",
                    "version": 1,
                    "text": "\n".join(
                        [
                            "def unique_ref_target() -> i32:",
                            "    return 7",
                            "",
                            "def main() -> i32:",
                            "    return unique_ref_target()",
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
            "id": 62,
            "method": "textDocument/rename",
            "params": {
                "textDocument": {"uri": rename_ast_uri},
                "position": {"line": 6, "character": 15},
                "newName": "callsite_rename",
            },
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
                            "from c import lsp_project_header.h",
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
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 140,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": uri},
                "position": {"line": 3, "character": 1},
            },
        }
    ),
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 141,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": uri},
                "position": {"line": 7, "character": 18},
            },
        }
    ),
    lsp_message(
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
    lsp_message(
        {
            "jsonrpc": "2.0",
            "id": 42,
            "method": "workspace/symbol",
            "params": {"query": "vendored_helper"},
        }
    ),
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
    lsp_message(
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
])
