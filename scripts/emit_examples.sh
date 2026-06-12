#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"

for source in "$repo_root"/examples/*.dd; do
    echo "example: ${source#$repo_root/}"
done
