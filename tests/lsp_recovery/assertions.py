import json

from protocol import response


def diagnostic_batches(messages, uri):
    return [
        item["params"]
        for item in messages
        if item.get("method") == "textDocument/publishDiagnostics"
        and item.get("params", {}).get("uri") == uri
    ]


def assert_versioned_diagnostics(messages, uri, versions):
    batches = diagnostic_batches(messages, uri)
    if not batches:
        raise AssertionError("server published no diagnostics")
    for batch in batches:
        if "version" not in batch:
            raise AssertionError(f"unversioned diagnostics: {batch}")
        if batch["version"] not in versions:
            raise AssertionError(f"diagnostics for unknown revision: {batch['version']}")


def assert_case(messages, case, requests, source, uri, version):
    batches = diagnostic_batches(messages, uri)
    matching = [batch for batch in batches if batch.get("version") == version]
    if not any(
        any(item.get("source") == case.diagnostic_source for item in batch["diagnostics"])
        for batch in matching
    ):
        raise AssertionError(f"{case.name}: missing {case.diagnostic_source} diagnostic: {matching}")

    marker = f"current_{case.name}"
    hover = response(messages, requests["hover"])
    if marker not in hover["contents"]["value"]:
        raise AssertionError(f"{case.name}: wrong hover: {hover}")

    definition = response(messages, requests["definition"])
    definitions = definition if isinstance(definition, list) else [definition]
    marker_line = source[: source.index(f"def {marker}")].count("\n")
    if not any(item["range"]["start"]["line"] == marker_line for item in definitions):
        raise AssertionError(f"{case.name}: wrong definition: {definition}")

    references = response(messages, requests["references"])
    if len(references) < 2:
        raise AssertionError(f"{case.name}: missing references: {references}")

    prepared = response(messages, requests["prepare_rename"])
    if prepared is None or prepared.get("placeholder") != marker:
        raise AssertionError(f"{case.name}: rename preparation failed: {prepared}")

    rename = response(messages, requests["rename"])
    edits = rename.get("changes", {}).get(uri, [])
    if len(edits) < 2 or any(edit["newText"] != f"renamed_{case.name}" for edit in edits):
        raise AssertionError(f"{case.name}: rename lost symbol identity: {rename}")

    labels = {item["label"] for item in response(messages, requests["completion"])}
    if marker not in labels:
        raise AssertionError(f"{case.name}: completion omitted {marker}: {sorted(labels)}")

    signatures = response(messages, requests["signature"])
    if not signatures or marker not in signatures["signatures"][0]["label"]:
        raise AssertionError(f"{case.name}: signature help disappeared: {signatures}")

    symbols = {item["name"] for item in response(messages, requests["symbols"])}
    if marker not in symbols or "usable" not in symbols:
        raise AssertionError(f"{case.name}: damaged symbols: {sorted(symbols)}")

    if not response(messages, requests["semantic"])["data"]:
        raise AssertionError(f"{case.name}: semantic tokens disappeared")
    if "i32" not in json.dumps(response(messages, requests["inlay"])):
        raise AssertionError(f"{case.name}: inlay hints disappeared")

    formatting = response(messages, requests["formatting"])
    if not isinstance(formatting, list):
        raise AssertionError(f"{case.name}: formatting failed: {formatting}")
