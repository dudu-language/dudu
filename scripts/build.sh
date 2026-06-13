#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$repo_root" -B "$repo_root/build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDUDU_BUILD_TESTS=ON \
    -DDUDU_STRICT=ON \
    -DDUDU_WARN_AS_ERROR=ON
cmake --build "$repo_root/build"
