initialize = next(item for item in responses if item.get("id") == 1)
assert initialize["result"]["capabilities"]["textDocumentSync"] == 2
assert initialize["result"]["capabilities"]["documentFormattingProvider"] is True
assert initialize["result"]["capabilities"]["referencesProvider"] is True
rename_provider = initialize["result"]["capabilities"]["renameProvider"]
assert rename_provider["prepareProvider"] is True
assert initialize["result"]["capabilities"]["codeActionProvider"] is True
assert initialize["result"]["capabilities"]["completionProvider"]["resolveProvider"] is True
assert initialize["result"]["capabilities"]["workspaceSymbolProvider"] is True
semantic_provider = initialize["result"]["capabilities"]["semanticTokensProvider"]
assert semantic_provider["full"] is True
assert "class" in semantic_provider["legend"]["tokenTypes"]

missing_position_definition = next(item for item in responses if item.get("id") == 59)
assert "error" in missing_position_definition, missing_position_definition
assert missing_position_definition["error"]["code"] == -32603, missing_position_definition
assert "position.line" in missing_position_definition["error"]["message"], (
    missing_position_definition
)

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
unused_fix_range = next(
    item for item in unused_diagnostics["params"]["diagnostics"]
    if item["message"] == "unused local: unused_value"
)["data"]["fixRange"]
assert unused_fix_range["start"]["line"] == 1
assert unused_fix_range["start"]["character"] == 0
assert unused_fix_range["end"]["line"] == 2
assert unused_fix_range["end"]["character"] == 0

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

def_keyword_hover = next(item for item in responses if item.get("id") == 140)
assert "keyword def" in def_keyword_hover["result"]["contents"]["value"]
assert "Declares a function or method." in def_keyword_hover["result"]["contents"]["value"]

call_hover = next(item for item in responses if item.get("id") == 141)
assert "def add(a: i32, b: i32) -> i32" in call_hover["result"]["contents"]["value"]

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

ast_amount_definition = next(item for item in responses if item.get("id") == 71)
assert ast_amount_definition["result"]["range"]["start"]["line"] == 4

ast_total_definition = next(item for item in responses if item.get("id") == 72)
assert ast_total_definition["result"]["range"]["start"]["line"] == 7

ast_extra_definition = next(item for item in responses if item.get("id") == 73)
assert ast_extra_definition["result"]["range"]["start"]["line"] == 5

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
assert isinstance(definition["result"], list)
assert any(item["range"]["start"]["line"] == 7 for item in definition["result"])
assert not any(item["range"]["start"]["line"] == 3 for item in definition["result"])

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
assert all(item["uri"] == uri for item in references["result"])
reference_starts = {
    (item["range"]["start"]["line"], item["range"]["start"]["character"])
    for item in references["result"]
}
assert (3, 4) in reference_starts
assert (7, 17) in reference_starts

declaration_references = next(item for item in responses if item.get("id") == 63)
assert all(item["uri"] != unrelated_uri for item in declaration_references["result"])
declaration_reference_starts = {
    (item["range"]["start"]["line"], item["range"]["start"]["character"])
    for item in declaration_references["result"]
    if item["uri"] == uri
}
assert declaration_reference_starts == {(3, 4), (7, 17)}

type_references = next(item for item in responses if item.get("id") == 61)
type_reference_starts = {
    (item["range"]["start"]["line"], item["range"]["start"]["character"])
    for item in type_references["result"]
}
assert (0, 6) in type_reference_starts
assert (8, 12) in type_reference_starts
assert (8, 21) in type_reference_starts

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

ast_use_rename = next(item for item in responses if item.get("id") == 62)
ast_use_rename_edits = ast_use_rename["result"]["changes"][rename_ast_uri]
assert rename_ast_unrelated_uri not in ast_use_rename["result"]["changes"]
ast_use_rename_starts = {
    (item["range"]["start"]["line"], item["range"]["start"]["character"], item["newText"])
    for item in ast_use_rename_edits
}
assert ast_use_rename_starts == {
    (0, 4, "callsite_rename"),
    (6, 11, "callsite_rename"),
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
assert native_symbol_names == ["main"]

native_hover = next(item for item in responses if item.get("id") == 9)
assert "dudu_native.dudu_native_add(i32, i32) -> i32" in native_hover["result"]["contents"]["value"]
assert "native i32(i32, i32)" in native_hover["result"]["contents"]["value"]
assert "Adds two native integers." in native_hover["result"]["contents"]["value"]

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

native_function_references = next(item for item in responses if item.get("id") == 69)
native_function_reference_ranges = {
    (
        item["range"]["start"]["line"],
        item["range"]["start"]["character"],
        item["range"]["end"]["character"],
    )
    for item in native_function_references["result"]
    if item["uri"] == native_uri
}
assert native_function_reference_ranges == {(9, 32, 47)}

direct_native_signature = next(item for item in responses if item.get("id") == 36)
assert "dudu_native_add(i32, i32) -> i32" in direct_native_signature["result"]["signatures"][0]["label"]
assert direct_native_signature["result"]["activeParameter"] == 1

direct_native_definition = next(item for item in responses if item.get("id") == 37)
assert direct_native_definition["result"]["uri"].endswith("/tests/fixtures/native_headers/simple_c.h")
assert direct_native_definition["result"]["range"]["start"]["line"] == 20

direct_native_function_references = next(item for item in responses if item.get("id") == 70)
direct_native_function_reference_ranges = {
    (
        item["range"]["start"]["line"],
        item["range"]["start"]["character"],
        item["range"]["end"]["character"],
    )
    for item in direct_native_function_references["result"]
    if item["uri"] == direct_native_uri
}
assert direct_native_function_reference_ranges == {(4, 11, 26)}

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
