#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$repo_root/scripts/test.sh"
"$repo_root/scripts/probe_optional.sh"
"$repo_root/scripts/probe_lsp_optional.sh"
