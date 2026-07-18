import tempfile
from pathlib import Path

from protocol import LspSession, notification, position, text_document


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


def is_diagnostic(message, uri, predicate):
    if message.get("method") != "textDocument/publishDiagnostics":
        return False
    params = message.get("params", {})
    return params.get("uri") == uri and predicate(params.get("diagnostics", []))


def run_macro_worker_failure(repo_root):
    with tempfile.TemporaryDirectory(prefix="dudu_lsp_macro_failure_") as temporary:
        root = Path(temporary)
        source_dir = root / "src"
        source_dir.mkdir()
        (root / "dudu.toml").write_text(
            'name = "lsp_macro_failure"\n'
            'entry = "src/main.dd"\n'
            'build_dir = "build"\n'
        )
        broken_macro = (
            "import dudu.ast as ast\n\n"
            "@macro\n"
            "def Explode(item: ast.ClassDecl) -> ast.Expansion:\n"
            '    absent = ast.find_attribute(item.attributes, "Never")\n'
            "    absent.value()\n"
            "    return ast.expansion()\n"
        )
        repaired_macro = (
            "import dudu.ast as ast\n\n"
            "@macro\n"
            "def Explode(item: ast.ClassDecl) -> ast.Expansion:\n"
            "    return ast.expansion()\n"
        )
        main_source = (
            "from macros import Explode\n\n"
            "@derive(Explode)\n"
            "class Broken:\n"
            "    value: i32\n\n"
            "def current_macro_worker(value: i32) -> i32:\n"
            "    return value + 1\n\n"
            "def main() -> i32:\n"
            "    return current_macro_worker(41)\n"
        )
        macro_path = source_dir / "macros.dd"
        main_path = source_dir / "main.dd"
        macro_path.write_text(broken_macro)
        main_path.write_text(main_source)
        macro_uri = macro_path.as_uri()
        main_uri = main_path.as_uri()

        session = LspSession(repo_root)
        session.request(1, "initialize", {"rootUri": root.as_uri()}, timeout=30)
        session.send(open_document(macro_uri, 1, broken_macro))
        session.send(open_document(main_uri, 1, main_source))
        failure = session.receive_until(
            lambda message: is_diagnostic(
                message,
                main_uri,
                lambda diagnostics: any(
                    item.get("source") == "dudu/macro"
                    and item.get("code") == "dudu.macro.worker"
                    for item in diagnostics
                ),
            ),
            timeout=30,
        )
        if not failure["params"]["diagnostics"]:
            raise AssertionError("macro worker failure did not publish a diagnostic")

        use = position(main_source, "current_macro_worker(41)", 2)
        hover = session.request(
            2,
            "textDocument/hover",
            {"textDocument": text_document(main_uri), "position": use},
            timeout=30,
        )
        if hover is None or "def current_macro_worker" not in hover["contents"]["value"]:
            raise AssertionError(f"macro worker failure erased current hover: {hover}")
        definition = session.request(
            3,
            "textDocument/definition",
            {"textDocument": text_document(main_uri), "position": use},
            timeout=30,
        )
        definitions = definition if isinstance(definition, list) else [definition]
        if not any(item and item.get("uri") == main_uri for item in definitions):
            raise AssertionError(f"macro worker failure erased current definition: {definition}")
        tokens = session.request(
            4,
            "textDocument/semanticTokens/full",
            {"textDocument": text_document(main_uri)},
            timeout=30,
        )
        if not tokens["data"]:
            raise AssertionError("macro worker failure erased semantic tokens")

        session.send(
            notification(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": macro_uri, "version": 2},
                    "contentChanges": [{"text": repaired_macro}],
                },
            )
        )
        session.receive_until(
            lambda message: is_diagnostic(message, main_uri, lambda diagnostics: not diagnostics),
            timeout=30,
        )
        repaired_hover = session.request(
            5,
            "textDocument/hover",
            {"textDocument": text_document(main_uri), "position": use},
            timeout=30,
        )
        if repaired_hover is None or "def current_macro_worker" not in repaired_hover["contents"]["value"]:
            raise AssertionError(f"macro repair did not recover without restart: {repaired_hover}")
        session.close(6)


def run_macro_recovery(repo_root):
    run_macro_worker_failure(repo_root)
