#!/usr/bin/env python3

from assertions import assert_case, assert_versioned_diagnostics
from cases import CASES, source_for
from modules import run_module_recovery
from protocol import notification, position, request, response, run_server, text_document


def case_requests(messages, next_id, uri, source, case):
    marker = f"current_{case.name}"
    call_position = position(source, f"{marker}(1)", 2)
    declaration_position = position(source, f"def {marker}", 4)
    completion_position = position(source, "    return result", len("    "))
    ids = {}
    for name, method, params in [
        ("hover", "textDocument/hover", {"textDocument": text_document(uri), "position": call_position}),
        ("definition", "textDocument/definition", {"textDocument": text_document(uri), "position": call_position}),
        ("references", "textDocument/references", {"textDocument": text_document(uri), "position": declaration_position}),
        ("prepare_rename", "textDocument/prepareRename", {"textDocument": text_document(uri), "position": call_position}),
        (
            "rename",
            "textDocument/rename",
            {"textDocument": text_document(uri), "position": call_position, "newName": f"renamed_{case.name}"},
        ),
        ("completion", "textDocument/completion", {"textDocument": text_document(uri), "position": completion_position}),
        (
            "signature",
            "textDocument/signatureHelp",
            {"textDocument": text_document(uri), "position": position(source, f"{marker}(1)", len(marker) + 1)},
        ),
        ("symbols", "textDocument/documentSymbol", {"textDocument": text_document(uri)}),
        ("semantic", "textDocument/semanticTokens/full", {"textDocument": text_document(uri)}),
        (
            "inlay",
            "textDocument/inlayHint",
            {
                "textDocument": text_document(uri),
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": source.count("\n") + 1, "character": 0},
                },
            },
        ),
        ("formatting", "textDocument/formatting", {"textDocument": text_document(uri), "options": {"tabSize": 4, "insertSpaces": True}}),
    ]:
        ids[name] = next_id
        messages.append(request(next_id, method, params))
        next_id += 1
    return next_id, ids


def run_damaged_case(repo_root, case, case_index):
    uri = f"file:///tmp/dudu_lsp_recovery_{case.name}.dd"
    source = source_for(case)
    messages = [request(1, "initialize", {})]
    version = case_index + 1
    messages.append(
        notification(
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
    )
    request_id, ids = case_requests(messages, 2, uri, source, case)
    messages.extend([request(request_id, "shutdown", None), notification("exit", None)])
    responses = run_server(repo_root, messages)
    assert_versioned_diagnostics(responses, uri, {version})
    assert_case(responses, case, ids, source, uri, version)


def run_repair_case(repo_root):
    case = CASES[0]
    uri = "file:///tmp/dudu_lsp_repair.dd"
    damaged = source_for(case)
    repaired = damaged.replace(case.damage, "def damaged() -> i32:\n    return 0")
    messages = [request(1, "initialize", {})]
    messages.append(
        notification(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "dudu", "version": 1, "text": damaged}},
        )
    )
    messages.append(
        notification(
            "textDocument/didChange",
            {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": repaired}]},
        )
    )
    messages.append(
        request(
            2,
            "textDocument/hover",
            {
                "textDocument": text_document(uri),
                "position": position(repaired, "current_unfinished_call(1)", 2),
            },
        )
    )
    messages.extend([request(3, "shutdown", None), notification("exit", None)])
    responses = run_server(repo_root, messages)
    assert_versioned_diagnostics(responses, uri, {1, 2})
    hover = response(responses, 2)
    if "current_unfinished_call" not in hover["contents"]["value"]:
        raise AssertionError(f"repaired document did not recover: {hover}")
    repaired_batches = [
        item["params"]["diagnostics"]
        for item in responses
        if item.get("method") == "textDocument/publishDiagnostics"
        and item.get("params", {}).get("uri") == uri
        and item["params"].get("version") == 2
    ]
    if not repaired_batches or repaired_batches[-1]:
        raise AssertionError(f"repaired diagnostics did not clear: {repaired_batches}")


def run_lifecycle_case(repo_root):
    uri = "file:///tmp/dudu_lsp_lifecycle.dd"
    obsolete_uri = "file:///tmp/dudu_lsp_obsolete_analysis.dd"
    valid = "def current() -> i32:\n    return 1\n"
    stale = "def stale() -> i32:\n    return missing_name\n"
    reopened = "def reopened() -> i32:\n    return 2\n"
    messages = [request(1, "initialize", {})]
    messages.append(
        notification(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "dudu", "version": 10, "text": valid}},
        )
    )
    messages.append(
        notification(
            "textDocument/didChange",
            {"textDocument": {"uri": uri, "version": 9}, "contentChanges": [{"text": stale}]},
        )
    )
    messages.append(
        request(
            2,
            "textDocument/hover",
            {"textDocument": text_document(uri), "position": position(valid, "current", 2)},
        )
    )
    messages.append(notification("textDocument/didClose", {"textDocument": text_document(uri)}))
    messages.append(
        notification(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "dudu", "version": 20, "text": reopened}},
        )
    )
    messages.append(
        request(
            3,
            "textDocument/hover",
            {"textDocument": text_document(uri), "position": position(reopened, "reopened", 2)},
        )
    )
    messages.append(
        notification(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": obsolete_uri,
                    "languageId": "dudu",
                    "version": 30,
                    "text": stale,
                }
            },
        )
    )
    messages.append(
        notification(
            "textDocument/didChange",
            {
                "textDocument": {"uri": obsolete_uri, "version": 31},
                "contentChanges": [{"text": valid}],
            },
        )
    )
    messages.extend([request(4, "shutdown", None), notification("exit", None)])
    responses = run_server(repo_root, messages)
    if "current" not in response(responses, 2)["contents"]["value"]:
        raise AssertionError("stale didChange replaced the current document")
    if "reopened" not in response(responses, 3)["contents"]["value"]:
        raise AssertionError("reopened document retained closed state")
    batches = [
        item["params"]
        for item in responses
        if item.get("method") == "textDocument/publishDiagnostics"
        and item.get("params", {}).get("uri") == uri
    ]
    close_index = next(
        i for i, batch in enumerate(batches) if "version" not in batch and not batch["diagnostics"]
    )
    if any(batch.get("version") == 10 for batch in batches[close_index + 1 :]):
        raise AssertionError(f"closed document published obsolete diagnostics: {batches}")
    if any(batch.get("version") == 9 for batch in batches):
        raise AssertionError(f"stale document version was analyzed: {batches}")
    obsolete_batches = [
        item["params"]
        for item in responses
        if item.get("method") == "textDocument/publishDiagnostics"
        and item.get("params", {}).get("uri") == obsolete_uri
    ]
    current_index = next(i for i, batch in enumerate(obsolete_batches) if batch.get("version") == 31)
    if any(batch.get("version") == 30 for batch in obsolete_batches[current_index + 1 :]):
        raise AssertionError(f"obsolete analysis published after current revision: {obsolete_batches}")


def run(repo_root):
    for index, case in enumerate(CASES):
        run_damaged_case(repo_root, case, index)
    run_repair_case(repo_root)
    run_lifecycle_case(repo_root)
    run_module_recovery(repo_root)
    print(f"lsp adversarial recovery checks passed ({len(CASES)} damaged states)")
