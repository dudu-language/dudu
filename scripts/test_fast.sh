#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

"$repo_root/scripts/build.sh" >/dev/null
ctest --test-dir "$repo_root/build" --output-on-failure

compile_and_expect simple_program 42
compile_and_expect c_direct_lowercase_macro 42
compile_and_expect c_lowercase_macro 42
compile_and_expect c_variadic_macro 42
compile_and_expect cpp_macro_bomb 42
compile_and_expect debug_asserts 42
"$repo_root/build/dudu" "$repo_root/tests/fixtures/freestanding_debug_assert.dd" \
    --emit-cpp "$repo_root/build/freestanding_debug_assert.cpp" -DTARGET_MODE=freestanding
grep -Fq "assert(((value == 42)))" "$repo_root/build/freestanding_debug_assert.cpp"
! grep -Fq "runtime_error" "$repo_root/build/freestanding_debug_assert.cpp"
compile_and_expect cpp_exceptions 42
compile_and_expect cpp_nested_native 42
compile_and_expect dudu_operator_overload 42
compile_and_expect dudu_operator_bool 42
compile_and_expect fixed_arrays 42
compile_and_expect array_explicit_initializer 42
compile_and_expect array_shape_inference 42
compile_and_expect array_row_index 7
compile_and_expect array_c_handoff 42
compile_and_expect array_slice_view 42
compile_and_expect matrix_math 26
compile_and_expect tensor_index_hook 42
compile_and_expect tensor_index_set_hook 42
compile_and_expect static_fields 42
compile_and_expect generic_box 42
compile_and_expect native_template_function 42
compile_and_expect native_scan_local 42

"$repo_root/build/duc" check "$repo_root/tests/fixtures/simple_program.dd"
"$repo_root/build/duc" fmt "$repo_root/tests/fixtures/simple_program.dd" --check
"$repo_root/scripts/test_lsp.sh"

echo "fast compiler checks passed"
