#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

"$repo_root/scripts/build.sh" >/dev/null

timeout_seconds="${DUDU_STDLIB_ALGORITHMS_TIMEOUT:-60}"
cpp="$repo_root/build/cpp_stdlib_algorithms.cpp"
bin="$repo_root/build/cpp_stdlib_algorithms"

timeout "$timeout_seconds" "$repo_root/build/dudu" \
    "$repo_root/tests/fixtures/cpp_stdlib_algorithms.dd" --emit-cpp "$cpp"
"${CXX:-c++}" -std=c++20 -I"$repo_root/tests/fixtures" "$cpp" -o "$bin"

set +e
"$bin"
status=$?
set -e

if [[ "$status" -ne 42 ]]; then
    echo "cpp_stdlib_algorithms returned $status, expected 42" >&2
    exit 1
fi

echo "cpp stdlib algorithms probe passed"
