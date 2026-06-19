#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

forbidden=(
    "statement_from_text"
    "Stmt::condition"
    "Stmt::target"
    "Stmt::value"
    "Stmt::return_type"
    "looks_like_dudu_type"
    "resolve_alias_ref_with_legacy_fallback"
)

for pattern in "${forbidden[@]}"; do
    if rg -n "$pattern" "$repo_root/src/dudu"; then
        echo "legacy AST migration pattern is forbidden: $pattern" >&2
        exit 1
    fi
done
