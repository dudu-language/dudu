#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$repo_root/scripts/test.sh"
"$repo_root/scripts/test_examples.sh"
"$repo_root/scripts/test_dependencies.sh"
if [[ "${DUDU_SKIP_OPTIONAL_NATIVE:-0}" == "1" ]]; then
    echo "skip optional native and LSP probes: DUDU_SKIP_OPTIONAL_NATIVE=1"
else
    "$repo_root/scripts/probe_optional.sh"
    "$repo_root/scripts/probe_lsp_optional.sh"
fi
