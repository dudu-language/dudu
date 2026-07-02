uri = "file:///tmp/dudu_lsp_bad.dd"
unrelated_uri = "file:///tmp/dudu_lsp_unrelated.dd"
native_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_native.dd"
missing_native_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_missing_native.dd"
native_pkg_uri = f"file://{repo_root}/tests/fixtures/lsp_pkg_project/main.dd"
native_pkg_config_uri = f"file://{repo_root}/tests/fixtures/lsp_pkg_project/dudu.toml"
rename_uri = f"file://{repo_root}/tests/fixtures/lsp_rename_main.dd"
rename_user_uri = f"file://{repo_root}/tests/fixtures/lsp_rename_user.dd"
rename_unrelated_uri = f"file://{repo_root}/tests/fixtures/lsp_rename_unrelated.dd"
rename_ast_uri = "file:///tmp/dudu_lsp_rename_ast.dd"
rename_ast_unrelated_uri = "file:///tmp/dudu_lsp_rename_ast_unrelated.dd"
lint_uri = "file:///tmp/dudu_lsp_lint.dd"
unused_uri = "file:///tmp/dudu_lsp_unused.dd"
shadow_uri = "file:///tmp/dudu_lsp_shadow.dd"
hover_locals_uri = "file:///tmp/dudu_lsp_hover_locals.dd"
hover_ast_locals_uri = "file:///tmp/dudu_lsp_hover_ast_locals.dd"
hover_docs_uri = "file:///tmp/dudu_lsp_hover_docs.dd"
hazard_uri = "file:///tmp/dudu_lsp_hazards.dd"
unknown_identifier_uri = "file:///tmp/dudu_lsp_unknown_identifier.dd"
import_graph_uri = f"file://{repo_root}/tests/fixtures/lsp_import_graph_entry.dd"
bad_config_uri = f"file://{repo_root}/tests/fixtures/lsp_bad_config/main.dd"
missing_pkg_uri = f"file://{repo_root}/tests/fixtures/lsp_missing_pkg/main.dd"
overload_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_overload.dd"
scope_uri = "file:///tmp/dudu_lsp_scope.dd"
direct_native_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_direct_native.dd"
from_import_uri = f"file://{repo_root}/tests/fixtures/dudu_lsp_from_import.dd"
lsp_include_uri = f"file://{repo_root}/tests/fixtures/lsp_include_project/src/main.dd"
ambiguous_import_uri = "file:///tmp/dudu_lsp_ambiguous_import.dd"
source = "\n".join(
    [
        "class Player:",
        "    hp: i32",
        "",
        "def add(a: i32, b: i32) -> i32:",
        "    return a + b",
        "",
        "def main() -> i32:   ",
        "    value: i32 = add(1, 2)",
        "    player: Player = Player(3)",
        "    player.hp",
        "    return True",
        "",
    ]
)
native_source = "\n".join(
    [
        "import lsp_workspace_helper as helper",
        "from cpp.path import native_headers/simple_cpp.hpp as native_cpp",
        "from c.path import ./native_headers/simple_c.h as dudu_native",
        "",
        "def main() -> i32:",
        "    widget: native_cpp.Widget = native_cpp.Widget(3)",
        "    widget.scaled(2)",
        "    helper.workspace_helper()",
        "    scaled: i32 = dudu_native.DUDU_NATIVE_SCALE(1)",
        "    return scaled + dudu_native.dudu_native_add(20, 22)",
        "# workspace_helper",
        "",
    ]
)
missing_native_source = "\n".join(
    [
        "from c.path import ./native_headers/does_not_exist.h",
        "",
        "def main() -> i32:",
        "    return 0",
        "",
    ]
)
messages = []
