#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if command -v clang-format >/dev/null 2>&1; then
    find . \
        -path ./build -prune -o \
        -path './build-*' -prune -o \
        -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
        -print0 | xargs -0 --no-run-if-empty clang-format -i
else
    echo "clang-format not found" >&2
    exit 1
fi
