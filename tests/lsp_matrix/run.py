#!/usr/bin/env python3

import pathlib
import shutil
import subprocess
import sys
import tempfile

from assert_actions import assert_action_behavior
from assert_core import assert_core_behavior
from assert_native import assert_native_behavior
from protocol import read_lsp_messages
from requests import build_requests
from run_advanced import run_advanced
from workspace import create_workspace


def run(repo_root):
    server = repo_root / "build" / "dudu-lsp"
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="dudu_lsp_matrix_"))
    try:
        workspace = create_workspace(tmp)
        proc = subprocess.run(
            [str(server)],
            input="".join(build_requests(workspace)).encode(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
        )
        if proc.returncode != 0:
            raise AssertionError(proc.stderr.decode(errors="replace"))

        messages = read_lsp_messages(proc.stdout)
        assert_core_behavior(messages, workspace)
        assert_native_behavior(messages, workspace)
        assert_action_behavior(messages, workspace, repo_root)
        run_advanced(repo_root, tmp / "advanced")
        print("lsp matrix checks passed")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    run(pathlib.Path(sys.argv[1]).resolve())
