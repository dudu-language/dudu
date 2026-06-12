#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"

for source in "$repo_root"/examples/*.dd; do
    out="$repo_root/build/$(basename "${source%.dd}").cpp"
    "$repo_root/build/dudu" "$source" --emit-cpp "$out"
    echo "emitted $out"
done
