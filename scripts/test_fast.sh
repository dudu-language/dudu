#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

"$repo_root/scripts/build.sh" >/dev/null
ctest --test-dir "$repo_root/build" --output-on-failure

compile_and_expect simple_program 42
compile_and_expect cpp_nested_native 42
compile_and_expect dudu_operator_overload 42

"$repo_root/build/duc" check "$repo_root/tests/fixtures/simple_program.dd"
"$repo_root/build/duc" fmt "$repo_root/tests/fixtures/simple_program.dd" --check

echo "fast compiler checks passed"
