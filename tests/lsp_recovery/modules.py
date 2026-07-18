import tempfile
from pathlib import Path

from protocol import LspSession, notification, position, request, response, run_server, text_document


def open_document(uri, version, source):
    return notification(
        "textDocument/didOpen",
        {
            "textDocument": {
                "uri": uri,
                "languageId": "dudu",
                "version": version,
                "text": source,
            }
        },
    )


def published_diagnostics(messages, uri):
    return [
        item["params"]
        for item in messages
        if item.get("method") == "textDocument/publishDiagnostics"
        and item.get("params", {}).get("uri") == uri
    ]


def run_removed_export(repo_root, root):
    helper = root / "helper.dd"
    main = root / "main.dd"
    valid_helper = "def exported(value: i32) -> i32:\n    return value + 1\n"
    broken_helper = "def replacement(value: i32) -> i32:\n    return value + 2\n"
    main_source = "from helper import exported\n\ndef main() -> i32:\n    return exported(1)\n"
    helper.write_text(valid_helper)
    main.write_text(main_source)
    helper_uri = helper.as_uri()
    main_uri = main.as_uri()

    messages = [request(1, "initialize", {"rootUri": root.as_uri()})]
    messages.extend(
        [open_document(helper_uri, 1, valid_helper), open_document(main_uri, 1, main_source)]
    )
    messages.append(
        notification(
            "textDocument/didChange",
            {
                "textDocument": {"uri": helper_uri, "version": 2},
                "contentChanges": [{"text": broken_helper}],
            },
        )
    )
    use = position(main_source, "exported(1)", 2)
    messages.append(
        request(2, "textDocument/hover", {"textDocument": text_document(main_uri), "position": use})
    )
    messages.append(
        request(
            3,
            "textDocument/definition",
            {"textDocument": text_document(main_uri), "position": use},
        )
    )
    messages.append(
        request(4, "textDocument/semanticTokens/full", {"textDocument": text_document(main_uri)})
    )
    messages.extend([request(5, "shutdown", None), notification("exit", None)])
    responses = run_server(repo_root, messages)

    if response(responses, 2) is not None:
        raise AssertionError("removed export retained stale hover")
    if response(responses, 3) not in (None, []):
        raise AssertionError("removed export retained stale definition")
    if not response(responses, 4)["data"]:
        raise AssertionError("dependent semantic tokens disappeared")
    diagnostics = published_diagnostics(responses, main_uri)
    if not any(
        any(item.get("source") == "dudu/sema" for item in batch["diagnostics"])
        for batch in diagnostics
    ):
        raise AssertionError(f"dependent diagnostics were not invalidated: {diagnostics}")


def run_restored_export(repo_root, root):
    helper = root / "helper.dd"
    main = root / "main.dd"
    broken_helper = "def replacement(value: i32) -> i32:\n    return value + 2\n"
    valid_helper = "def exported(value: i32) -> i32:\n    return value + 1\n"
    main_source = "from helper import exported\n\ndef main() -> i32:\n    return exported(1)\n"
    helper.write_text(broken_helper)
    main.write_text(main_source)
    helper_uri = helper.as_uri()
    main_uri = main.as_uri()

    messages = [request(1, "initialize", {"rootUri": root.as_uri()})]
    messages.extend(
        [open_document(helper_uri, 1, broken_helper), open_document(main_uri, 1, main_source)]
    )
    messages.append(
        notification(
            "textDocument/didChange",
            {
                "textDocument": {"uri": helper_uri, "version": 2},
                "contentChanges": [{"text": valid_helper}],
            },
        )
    )
    use = position(main_source, "exported(1)", 2)
    messages.append(
        request(2, "textDocument/hover", {"textDocument": text_document(main_uri), "position": use})
    )
    messages.append(
        request(
            3,
            "textDocument/definition",
            {"textDocument": text_document(main_uri), "position": use},
        )
    )
    messages.extend([request(4, "shutdown", None), notification("exit", None)])
    responses = run_server(repo_root, messages)

    if "def exported(value: i32) -> i32" not in response(responses, 2)["contents"]["value"]:
        raise AssertionError("restored export hover did not recover")
    definition = response(responses, 3)
    definitions = definition if isinstance(definition, list) else [definition]
    if not any(item.get("uri") == helper_uri for item in definitions):
        raise AssertionError(f"restored export definition did not recover: {definition}")
    diagnostics = published_diagnostics(responses, main_uri)
    if not diagnostics or diagnostics[-1]["diagnostics"]:
        raise AssertionError(f"restored dependent diagnostics did not clear: {diagnostics}")


def run_missing_module_uses_current_revision(repo_root, root):
    main = root / "main.dd"
    valid = "def old_answer() -> i32:\n    return 41\n"
    damaged = (
        "import module_that_does_not_exist\n\n"
        "def current_answer() -> i32:\n"
        "    return 42\n\n"
        "def main() -> i32:\n"
        "    return current_answer()\n"
    )
    main.write_text(valid)
    uri = main.as_uri()

    messages = [request(1, "initialize", {"rootUri": root.as_uri()})]
    messages.append(open_document(uri, 1, valid))
    messages.append(
        notification(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": damaged}],
            },
        )
    )
    use = position(damaged, "current_answer()", 2, occurrence=1)
    messages.append(
        request(2, "textDocument/hover", {"textDocument": text_document(uri), "position": use})
    )
    messages.append(
        request(
            3,
            "textDocument/definition",
            {"textDocument": text_document(uri), "position": use},
        )
    )
    messages.append(
        request(4, "textDocument/documentSymbol", {"textDocument": text_document(uri)})
    )
    messages.append(
        request(5, "textDocument/semanticTokens/full", {"textDocument": text_document(uri)})
    )
    messages.extend([request(6, "shutdown", None), notification("exit", None)])
    responses = run_server(repo_root, messages)

    hover = response(responses, 2)
    if hover is None or "def current_answer() -> i32" not in hover["contents"]["value"]:
        raise AssertionError(f"missing-module edit returned stale hover: {hover}")
    definition = response(responses, 3)
    definitions = definition if isinstance(definition, list) else [definition]
    expected_line = damaged[: damaged.index("def current_answer")].count("\n")
    if not any(item["range"]["start"]["line"] == expected_line for item in definitions):
        raise AssertionError(f"missing-module edit returned stale definition: {definition}")
    symbols = {item["name"] for item in response(responses, 4)}
    if "current_answer" not in symbols or "old_answer" in symbols:
        raise AssertionError(f"missing-module edit returned stale symbols: {symbols}")
    if not response(responses, 5)["data"]:
        raise AssertionError("missing-module edit lost semantic tokens")
    diagnostics = published_diagnostics(responses, uri)
    if not any(
        any(item.get("code") == "dudu.sema.missing_module" for item in batch["diagnostics"])
        for batch in diagnostics
        if batch.get("version") == 2
    ):
        raise AssertionError(f"missing-module diagnostic was not published: {diagnostics}")


def run_watched_file_rename(repo_root, root):
    helper = root / "helper.dd"
    moved = root / "moved.dd"
    main = root / "main.dd"
    helper_source = "def exported(value: i32) -> i32:\n    return value + 1\n"
    main_source = "from helper import exported\n\ndef main() -> i32:\n    return exported(1)\n"
    helper.write_text(helper_source)
    main.write_text(main_source)
    main_uri = main.as_uri()
    use = position(main_source, "exported(1)", 2)

    session = LspSession(repo_root)
    session.request(1, "initialize", {"rootUri": root.as_uri()})
    session.send(open_document(main_uri, 1, main_source))
    initial = session.request(
        2, "textDocument/definition", {"textDocument": text_document(main_uri), "position": use}
    )
    initial_definitions = initial if isinstance(initial, list) else [initial]
    if not any(item.get("uri") == helper.as_uri() for item in initial_definitions):
        raise AssertionError(f"initial imported definition failed: {initial}")

    helper.rename(moved)
    session.send(
        notification(
            "workspace/didChangeWatchedFiles",
            {
                "changes": [
                    {"uri": helper.as_uri(), "type": 3},
                    {"uri": moved.as_uri(), "type": 1},
                ]
            },
        )
    )
    removed = session.request(
        3, "textDocument/definition", {"textDocument": text_document(main_uri), "position": use}
    )
    if removed not in (None, []):
        raise AssertionError(f"renamed module retained stale definition: {removed}")

    moved.rename(helper)
    session.send(
        notification(
            "workspace/didChangeWatchedFiles",
            {
                "changes": [
                    {"uri": moved.as_uri(), "type": 3},
                    {"uri": helper.as_uri(), "type": 1},
                ]
            },
        )
    )
    restored = session.request(
        4, "textDocument/definition", {"textDocument": text_document(main_uri), "position": use}
    )
    restored_definitions = restored if isinstance(restored, list) else [restored]
    if not any(item.get("uri") == helper.as_uri() for item in restored_definitions):
        raise AssertionError(f"restored module definition did not recover: {restored}")
    session.close(5)


def run_workspace_folder_changes(repo_root, root):
    initial_root = root / "initial"
    added_root = root / "added"
    initial_root.mkdir()
    added_root.mkdir()
    main = initial_root / "main.dd"
    added = added_root / "extra.dd"
    main_source = "def main() -> i32:\n    return 0\n"
    main.write_text(main_source)
    added.write_text("def added_symbol() -> i32:\n    return 42\n")
    main_uri = main.as_uri()

    session = LspSession(repo_root)
    session.request(1, "initialize", {"rootUri": initial_root.as_uri()})
    session.send(open_document(main_uri, 1, main_source))
    before = session.request(2, "workspace/symbol", {"query": "added_symbol"})
    if before:
        raise AssertionError(f"symbol outside workspace leaked before folder add: {before}")

    session.send(
        notification(
            "workspace/didChangeWorkspaceFolders",
            {
                "event": {
                    "added": [{"uri": added_root.as_uri(), "name": "added"}],
                    "removed": [],
                }
            },
        )
    )
    present = session.request(3, "workspace/symbol", {"query": "added_symbol"})
    if not any(item.get("name") == "added_symbol" for item in present):
        raise AssertionError(f"added workspace folder was not indexed: {present}")

    session.send(
        notification(
            "workspace/didChangeWorkspaceFolders",
            {
                "event": {
                    "added": [],
                    "removed": [{"uri": added_root.as_uri(), "name": "added"}],
                }
            },
        )
    )
    removed = session.request(4, "workspace/symbol", {"query": "added_symbol"})
    if removed:
        raise AssertionError(f"removed workspace folder retained symbols: {removed}")
    session.close(5)


def run_module_recovery(repo_root):
    with tempfile.TemporaryDirectory(prefix="dudu_lsp_module_recovery_") as directory:
        root = Path(directory)
        run_removed_export(repo_root, root)
        run_restored_export(repo_root, root)
        run_missing_module_uses_current_revision(repo_root, root)
        run_watched_file_rename(repo_root, root)
        run_workspace_folder_changes(repo_root, root)
