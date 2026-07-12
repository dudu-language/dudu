#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

repo_root = sys.argv[1]
smoke_dir = Path(__file__).resolve().parent / "lsp_smoke"
context = {
    "Path": Path,
    "repo_root": repo_root,
    "subprocess": subprocess,
}

for chunk in [
    'protocol.py',
    'case_setup.py',
    'case_messages_00.py',
    'case_messages_01.py',
    'case_messages_02.py',
    'case_messages_03.py',
    'case_expectations.py',
    'run_server.py',
    'assertions_00.py',
    'assertions_01.py',
]:
    path = smoke_dir / chunk
    code = compile(path.read_text(), str(path), "exec")
    exec(code, context)
