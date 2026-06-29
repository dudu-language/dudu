
diagnostic_uris = [
    uri,
    unknown_identifier_uri,
    missing_native_uri,
    lint_uri,
    unused_uri,
    shadow_uri,
    hazard_uri,
    bad_config_uri,
    native_pkg_uri,
    missing_pkg_uri,
]
diagnostic_messages = [
    lsp_message(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didSave",
            "params": {"textDocument": {"uri": diagnostic_uri}},
        }
    )
    for diagnostic_uri in diagnostic_uris
]
messages[-2:-2] = diagnostic_messages

