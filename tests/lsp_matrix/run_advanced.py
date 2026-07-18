import subprocess

from assert_advanced import assert_advanced_behavior
from protocol import read_lsp_messages
from requests_advanced import build_advanced_requests
from workspace_advanced import create_advanced_workspace


def run_advanced(repo_root, tmp):
    workspace = create_advanced_workspace(tmp)
    proc = subprocess.run(
        [str(repo_root / "build" / "dudu-lsp")],
        input="".join(build_advanced_requests(workspace)).encode(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=15,
        check=False,
    )
    if proc.returncode != 0:
        raise AssertionError(proc.stderr.decode(errors="replace"))
    assert_advanced_behavior(read_lsp_messages(proc.stdout), workspace)
