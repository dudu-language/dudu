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
    "find_top_level_assignment"
    "find_top_level_member_dot"
    "find_top_level_binary_operator"
    "find_top_level_arrow"
    "split_top_level\\("
    "find_top_level_char"
    "is_member_path\\("
    "lower_cpp_type\\(const std::string"
    "lower_cpp_type\\(std::string"
    "is_integer_type\\(type_ref_head_name"
    "bool is_integer_type\\(std::string"
    "normalize_cpp_type_artifacts\\(std::string"
    "std::string normalize_cpp_type_artifacts\\(const TypeRef&"
    "infer_cpp_escape_expr\\("
    "infer_emitted_local_type_text"
    "assignment_error\\(const TypeRef& expected, const Expr& expr, const std::string"
    "type_ref_text\\(type\\) == \"\\.\\.\\.\""
    "template_args_lookup_text"
    "signature_param_type_text"
    "signature_return_type_text"
    "signature_text\\("
    "constructor_param_type_text"
    "shape_text\\("
    "MatchCheckCallbacks"
    "signature_param_ref"
    "signature_param_text"
    "NativeArgType"
    "function_receiver_type_text"
    "unwrap_receiver_type\\("
    "parse_native_type_text\\(type_ref_text"
    "render_type\\(const TypeRef"
    "lower_template_call_arg\\("
    "native_template_call_base"
    "native_template_type_refs"
    "native_template_binding_type_ref"
    "native_template_pack_placeholder\\(std::string"
    "replace_template_bindings"
    "replace_pack_binding"
    "join_type_refs"
    "explicit_array_shape_text"
    "bases\\.insert\\(type_ref_text"
    "native_function_key"
    "method_key\\("
    "template_method_name"
    "decorator_first_arg_text"
    "decorator_arg_list_text"
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

if rg -n "std::isupper" "$repo_root/src/dudu/cpp_expr_emit.cpp"; then
    echo "member emission must use symbol metadata, not uppercase spelling heuristics" >&2
    exit 1
fi

if rg -n "std::isupper" "$repo_root/src/dudu/cpp_expr_call_emit.cpp"; then
    echo "call emission must use symbol/type metadata, not uppercase spelling heuristics" >&2
    exit 1
fi

if rg -n "std::string text;" "$repo_root/src/dudu/ast.hpp"; then
    echo "raw text payload fields do not belong in the core Dudu AST" >&2
    exit 1
fi

if rg -n "\\braw_type\\b|\\braw_receiver_type\\b|\\braw_native_alias\\b" \
    "$repo_root/src/dudu/sema_index.cpp" "$repo_root/src/dudu/sema_index_type_ref.cpp" \
    "$repo_root/src/dudu/sema_index_type_ref.hpp"; then
    echo "index sema uses structured TypeRef receiver metadata; raw names belong only at raw/native boundaries" >&2
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

awk '
    /struct ClassDecl/ {
        in_class = 1
        has_identity = 0
    }
    in_class && /NativeSymbolId identity/ { has_identity = 1 }
    in_class && /^};/ {
        if (!has_identity) {
            print FILENAME ":" FNR ": ClassDecl must carry NativeSymbolId identity for native class imports"
            bad = 1
        }
        in_class = 0
    }
    END { exit bad ? 1 : 0 }
' "$repo_root/src/dudu/ast.hpp"

allowed_lower_cpp_type_spelling='src/dudu/cpp_expr_builtins.cpp|src/dudu/cpp_lower.cpp|src/dudu/cpp_raw_escape_templates.cpp|src/dudu/cpp_type.cpp|src/dudu/cpp_lower.hpp'
if rg -n "lower_cpp_type_spelling" "$repo_root/src/dudu" |
    grep -Ev "$allowed_lower_cpp_type_spelling"; then
    echo "lower_cpp_type_spelling is only allowed at explicit raw C++ escape/native spelling boundaries" >&2
    exit 1
fi

awk '
    /struct AstLintLocal/ { in_local = 1 }
    in_local && /int (line|column)/ {
        print FILENAME ":" FNR ": AstLintLocal should only carry name and TypeRef"
        bad = 1
    }
    in_local && /^};/ { in_local = 0 }
    END { exit bad ? 1 : 0 }
' "$repo_root/src/dudu/language_server_ast_lints.cpp"
