from_import_definition = next(item for item in responses if item.get("id") == 38)
assert from_import_definition["result"]["uri"].endswith("/tests/fixtures/lsp_workspace_helper.dd")
assert from_import_definition["result"]["range"]["start"]["line"] == 0

native_member_definition = next(item for item in responses if item.get("id") == 31)
assert native_member_definition["result"]["uri"].endswith("/tests/fixtures/native_headers/simple_cpp.hpp")
assert native_member_definition["result"]["range"]["start"]["line"] == 9

native_type_definition = next(item for item in responses if item.get("id") == 60)
assert native_type_definition["result"]["uri"].endswith("/tests/fixtures/native_headers/simple_cpp.hpp")
assert native_type_definition["result"]["range"]["start"]["line"] == 3

native_type_hover = next(item for item in responses if item.get("id") == 65)
assert "native type = dudu_native.Widget" in native_type_hover["result"]["contents"]["value"]
assert "resolves to native class dudu_native.Widget" in native_type_hover["result"]["contents"]["value"]

native_type_references = next(item for item in responses if item.get("id") == 67)
native_type_reference_ranges = {
    (
        item["range"]["start"]["line"],
        item["range"]["start"]["character"],
        item["range"]["end"]["character"],
    )
    for item in native_type_references["result"]
    if item["uri"] == native_uri
}
assert (5, 12, 29) in native_type_reference_ranges
assert (5, 43, 49) in native_type_reference_ranges

native_constructor_references = next(item for item in responses if item.get("id") == 68)
native_constructor_reference_ranges = {
    (
        item["range"]["start"]["line"],
        item["range"]["start"]["character"],
        item["range"]["end"]["character"],
    )
    for item in native_constructor_references["result"]
    if item["uri"] == native_uri
}
assert native_constructor_reference_ranges == native_type_reference_ranges

native_semantic_tokens = next(item for item in responses if item.get("id") == 66)
native_decoded = []
line = 0
character = 0
for i in range(0, len(native_semantic_tokens["result"]["data"]), 5):
    delta_line, delta_start, length, token_type, modifiers = native_semantic_tokens["result"][
        "data"
    ][i : i + 5]
    line += delta_line
    character = character + delta_start if delta_line == 0 else delta_start
    native_decoded.append((line, character, length, legend[token_type], modifiers))
assert (5, 12, 17, "class", 16) in native_decoded

native_code_actions = next(item for item in responses if item.get("id") == 25)
organize_imports = next(
    item for item in native_code_actions["result"] if item["title"] == "Organize imports"
)
organize_edit = organize_imports["edit"]["changes"][native_uri][0]
assert organize_edit["range"]["start"]["line"] == 0
assert organize_edit["range"]["end"]["line"] == 3
assert organize_edit["newText"].splitlines() == [
    "from c.path import ./native_headers/simple_c.h as dudu_native",
    "from cpp.path import native_headers/simple_cpp.hpp as native_cpp",
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

ambiguous_import_actions = next(item for item in responses if item.get("id") == 64)
assert not any(
    item["title"].startswith("Import ambiguous_helper ")
    for item in ambiguous_import_actions["result"]
)

native_config_actions = next(item for item in responses if item.get("id") == 27)
assert not any(
    item["title"].startswith("Add pkg-config package ")
    for item in native_config_actions["result"]
)

workspace_rename = next(item for item in responses if item.get("id") == 28)
assert rename_uri in workspace_rename["result"]["changes"]
assert rename_user_uri in workspace_rename["result"]["changes"]
assert rename_unrelated_uri not in workspace_rename["result"]["changes"]
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
