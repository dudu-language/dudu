import subprocess

from protocol import item_with_title, publish_diagnostics, response


def offset_at(text, position):
    lines = text.splitlines(keepends=True)
    line = position["line"]
    if line < 0 or line >= len(lines):
        raise AssertionError(f"edit line outside document: {position!r}")
    return sum(len(item) for item in lines[:line]) + position["character"]


def apply_edits(path, edits):
    text = path.read_text()
    replacements = []
    for edit in edits:
        edit_range = edit["range"]
        replacements.append(
            (
                offset_at(text, edit_range["start"]),
                offset_at(text, edit_range["end"]),
                edit.get("newText", ""),
            )
        )
    for start, end, replacement in sorted(replacements, reverse=True):
        text = text[:start] + replacement + text[end:]
    path.write_text(text)


def assert_checks(repo_root, path, label):
    result = subprocess.run(
        [str(repo_root / "build" / "duc"), "check", str(path), "--quiet"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=10,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"{label} did not produce valid Dudu:\n{result.stdout}{result.stderr}\n{path.read_text()}"
        )


def assert_action_behavior(messages, workspace, repo_root):
    missing = workspace.missing
    missing_symbol = workspace.missing_symbol
    disorganized_imports = workspace.disorganized_imports
    lint_quickfix = workspace.lint_quickfix

    missing_diags = publish_diagnostics(messages, missing.as_uri())
    if not missing_diags or not missing_diags[-1]:
        raise AssertionError("missing import fixture did not publish diagnostics")
    missing_import_actions = response(messages, 107)
    missing_import_action = item_with_title(missing_import_actions, "Import MissingThing from available_symbol")
    missing_symbol_edits = missing_import_action.get("edit", {}).get("changes", {}).get(missing_symbol.as_uri(), [])
    expected_import = "from available_symbol import MissingThing\n"
    if not any(
        edit.get("newText") == expected_import
        and edit.get("range", {}).get("start", {}) == {"line": 0, "character": 0}
        for edit in missing_symbol_edits
    ):
        raise AssertionError(f"missing import quick fix edit: {missing_import_action!r}")
    apply_edits(missing_symbol, missing_symbol_edits)
    assert_checks(repo_root, missing_symbol, "missing-import quick fix")
    organize_actions = response(messages, 108)
    organize_action = item_with_title(organize_actions, "Organize imports")
    organize_edits = organize_action.get("edit", {}).get("changes", {}).get(disorganized_imports.as_uri(), [])
    expected_organized = "import alpha\nimport zeta\n"
    if not any(
        edit.get("newText") == expected_organized
        and edit.get("range", {}).get("start", {}) == {"line": 0, "character": 0}
        and edit.get("range", {}).get("end", {}) == {"line": 2, "character": 0}
        for edit in organize_edits
    ):
        raise AssertionError(f"missing organize-imports edit: {organize_action!r}")
    apply_edits(disorganized_imports, organize_edits)
    assert_checks(repo_root, disorganized_imports, "organize-imports action")
    lint_actions = response(messages, 109)
    lint_action = item_with_title(lint_actions, "Remove unused local")
    lint_edits = lint_action.get("edit", {}).get("changes", {}).get(lint_quickfix.as_uri(), [])
    if not any(
        edit.get("newText") == ""
        and edit.get("range", {}).get("start", {}) == {"line": 1, "character": 0}
        and edit.get("range", {}).get("end", {}) == {"line": 2, "character": 0}
        for edit in lint_edits
    ):
        raise AssertionError(f"missing lint quick fix edit: {lint_action!r}")
    apply_edits(lint_quickfix, lint_edits)
    assert_checks(repo_root, lint_quickfix, "unused-local quick fix")
    resolved_completion = response(messages, 110)
    if resolved_completion.get("label") != "mix":
        raise AssertionError(f"completion resolve lost label: {resolved_completion!r}")
    if resolved_completion.get("detail") != "def mix(left: i32, right: i32) -> i32":
        raise AssertionError(f"completion resolve lost detail: {resolved_completion!r}")
    documentation = resolved_completion.get("documentation", {})
    if documentation.get("kind") != "markdown" or "Mixes two numbers" not in documentation.get("value", ""):
        raise AssertionError(f"completion resolve lost documentation: {resolved_completion!r}")
