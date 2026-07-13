#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
duc_bin="${DUC_BIN:-$repo_root/build/duc}"

if [[ ! -x "$duc_bin" ]]; then
    echo "site check requires a built duc at $duc_bin" >&2
    exit 1
fi

python3 "$repo_root/scripts/check_site.py" "$repo_root"

fixtures=(
    tests/fixtures/containers.dd
    tests/fixtures/array_literal_inference.dd
    tests/fixtures/numeric_literal_context.dd
    tests/fixtures/compile_time_programming.dd
    tests/fixtures/generics_reference.dd
    tests/fixtures/native_template_function.dd
    tests/fixtures/cpp_macro_bomb.dd
    tests/fixtures/allocation_native_interop.dd
    tests/fixtures/array_indexing_tutorial.dd
)

for fixture in "${fixtures[@]}"; do
    "$duc_bin" "$repo_root/$fixture" --check
    printf 'sample passed: %s\n' "$fixture"
done

echo "public site and advertised compiler samples passed"
