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
    "lower_cpp_type\\(type_ref_head_name"
    "lower_function_signature_type"
    "lower_cpp_type\\(type\\.substr"
    "lower_template_arg_type"
    "lower_template_type\\(std::string_view"
    "fixed_array_dimensions"
    "fixed_array_base"
    "ArrayShorthand"
    "lower_array_shorthand_type"
    "collect_array_shorthand"
)

for pattern in "${forbidden[@]}"; do
    if rg -n "$pattern" "$repo_root/src/dudu"; then
        echo "legacy AST migration pattern is forbidden: $pattern" >&2
        exit 1
    fi
done

if rg -n "expr\\.text" "$repo_root/src/dudu" \
    --glob '!ast_expr_token_core.cpp' \
    --glob '!ast_parse_utils.cpp'; then
    echo "Expr.text is parser construction metadata only; semantic consumers must use structured Expr fields" >&2
    exit 1
fi
