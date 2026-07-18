#!/usr/bin/env python3

import pathlib
import sys


repo_root = pathlib.Path(sys.argv[1]).resolve()
sys.path.insert(0, str(repo_root / "tests" / "lsp_recovery"))

from run import run  # noqa: E402


run(repo_root)
