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
    "is_cpp_associated_type_binding"
    "compound_assign_pos"
    "find_top_level_logical"
    "find_top_level_comparison"
    "top_level_comparison_text"
    "lower_cpp_type\\(const std::string"
    "lower_cpp_type\\(std::string"
    "is_integer_type\\(type_ref_head_name"
    "bool is_integer_type\\(std::string"
    "normalize_cpp_type_artifacts\\(std::string"
    "infer_cpp_escape_expr\\("
    "assignment_error\\(const TypeRef& expected, const Expr& expr, const std::string"
    "type_ref_text\\(type\\) == \"\\.\\.\\.\""
    "function_receiver_type_text"
    "render_type\\(const TypeRef"
    "lower_template_call_arg\\("
)

for pattern in "${forbidden[@]}"; do
    if rg -n "$pattern" "$repo_root/src/dudu"; then
        echo "legacy AST migration pattern is forbidden: $pattern" >&2
        exit 1
    fi
done

if rg -n "expr\\.text" "$repo_root/src/dudu"; then
    echo "Expr.text has been removed; use structured Expr fields and source ranges" >&2
    exit 1
fi

if rg -n "type\\.text|left\\.text|right\\.text" "$repo_root/src/dudu"; then
    echo "TypeRef.text has been removed; use structured TypeRef fields and source ranges" >&2
    exit 1
fi

if rg -n "ends_with\\(\"\\.(reference|const_reference|iterator|const_iterator)\"\\)" \
    "$repo_root/src/dudu/sema_ops.cpp"; then
    echo "native associated operator suffix checks belong in type_compat_native" >&2
    exit 1
fi

if rg -n "std::string text;" "$repo_root/src/dudu/ast.hpp"; then
    echo "raw text payload fields do not belong in the core Dudu AST" >&2
    exit 1
fi

awk '
    /struct Native(Type|Value|Function)Decl/ { in_native = 1 }
    in_native && /std::string type;/ {
        print FILENAME ":" FNR ": native declarations must use native_spelling"
        bad = 1
    }
    in_native && /std::vector<std::string> params;/ {
        print FILENAME ":" FNR ": native functions must use param_native_spellings"
        bad = 1
    }
    in_native && /std::string return_type;/ {
        print FILENAME ":" FNR ": native functions must use return_native_spelling"
        bad = 1
    }
    in_native && /^};/ { in_native = 0 }
    END { exit bad ? 1 : 0 }
' "$repo_root/src/dudu/ast.hpp"

awk '
    /struct Native(Type|Value|Function|Macro|Namespace)Decl/ {
        in_native = 1
        name = $2
        sub(/\{.*/, "", name)
        has_identity = 0
    }
    in_native && /NativeSymbolId identity/ { has_identity = 1 }
    in_native && /^};/ {
        if (!has_identity) {
            print FILENAME ":" FNR ": " name " must carry NativeSymbolId identity"
            bad = 1
        }
        in_native = 0
    }
    END { exit bad ? 1 : 0 }
' "$repo_root/src/dudu/ast.hpp"
