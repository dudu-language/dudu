#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 "$repo_root/scripts/test_lsp_smoke.py" "$repo_root"
echo "lsp smoke checks passed"
python3 "$repo_root/scripts/test_lsp_recovery.py" "$repo_root"
"$repo_root/scripts/test_lsp_matrix.sh"
