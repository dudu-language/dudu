#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

"$repo_root/scripts/check_ast_migration_guards.sh"
bash -n "$repo_root/scripts/install-local.sh"
"$repo_root/scripts/build.sh" >/dev/null
ctest --test-dir "$repo_root/build" --output-on-failure

compile_and_expect simple_program 42
compile_and_expect c_direct_lowercase_macro 42
compile_and_expect c_lowercase_macro 42
compile_and_expect c_variadic_macro 42
compile_and_expect cpp_macro_bomb 42
compile_and_expect debug_asserts 42
compile_and_expect enum_match 49
compile_and_expect payload_enum_match 82
compile_and_expect recursive_enum_expr 42
compile_and_expect result_option 42
compile_and_expect sum_type_events 42
compile_and_expect sum_type_tokens 42
compile_and_expect anonymous_variant 42
"$repo_root/build/dudu" "$repo_root/tests/fixtures/freestanding_debug_assert.dd" \
    --emit-cpp "$repo_root/build/freestanding_debug_assert.cpp" -DTARGET_MODE=freestanding
grep -Fq "assert(((value == 42)))" "$repo_root/build/freestanding_debug_assert.cpp"
! grep -Fq "runtime_error" "$repo_root/build/freestanding_debug_assert.cpp"
compile_and_expect cpp_exceptions 42
compile_and_expect cpp_escape_expr 42
compile_and_expect cpp_nested_native 42
compile_and_expect cpp_non_type_template_arg 3
compile_and_expect dudu_operator_overload 42
compile_and_expect dudu_operator_overload_rhs 42
compile_and_expect dudu_operator_bool 42
compile_and_expect member_index_assignment_ast 44
compile_and_expect member_index_ast_path 42
compile_and_expect literal_symbol_context 20
compile_and_expect member_expr_receiver 42
compile_and_expect method_expr_receiver 42
compile_and_expect named_callback 42
compile_and_expect function_values 42
compile_and_expect fixed_arrays 42
compile_and_expect array_explicit_initializer 42
compile_and_expect array_shape_inference 42
compile_and_expect array_row_index 7
compile_and_expect array_c_handoff 42
compile_and_expect array_slice_view 42
compile_and_expect array_open_slice 42
compile_and_expect array_row_slice 42
compile_and_expect array_column_slice 42
compile_and_expect array_channel_slice 45
compile_and_expect array_step_slice 42
compile_and_expect array_image_kernel 42
compile_and_expect swizzle_vec2 42
compile_and_expect swizzle_rgba 42
compile_and_expect swizzle_stpq 42
compile_and_expect swizzle_expr_receiver 42
compile_and_expect swizzle_width_result 42
compile_and_expect cpp_swizzle 42
compile_and_expect swizzle_assignment 42
compile_and_expect matrix_math 26
compile_and_expect tensor_index_hook 42
compile_and_expect tensor_index_set_hook 42
compile_and_expect static_fields 42
compile_and_expect static_class_alias 42
compile_and_expect static_class_method_alias 42
compile_and_expect static_generic_method_alias 42
compile_and_expect generic_arena_handle 42
compile_and_expect generic_box 42
compile_and_expect generic_identity 42
compile_and_expect generic_inferred_calls 42
compile_and_expect generic_inferred_structured 42
compile_and_expect generic_method 42
compile_and_expect generic_method_inferred 42
compile_and_expect generic_method_multi 42
compile_and_expect generic_method_return_context 42
compile_and_expect generic_method_receiver_ast 42
compile_and_expect generic_pair 42
compile_and_expect generic_result_helpers 42
compile_and_expect generic_sort_by 42
compile_and_expect generic_span_sum 42
compile_and_expect generic_stack 42
compile_and_expect generic_substitution_shapes 42
compile_and_expect generic_vec2 42
compile_and_expect inheritance_abstract 42
compile_and_expect inheritance_generic_base 42
compile_and_expect inheritance_super_method 42
compile_and_expect inheritance_super_init 42
compile_and_expect inheritance_virtual_override 42
compile_and_expect inheritance_multiple_interfaces 42
compile_and_expect inheritance_virtual_destructor 42
compile_and_expect inheritance_virtual_drop 42
compile_and_expect inheritance_autograd_graph 42
"$repo_root/build/dudu" "$repo_root/tests/fixtures/inheritance_virtual_destructor.dd" \
    --emit-cpp "$repo_root/build/inheritance_virtual_destructor.cpp"
grep -Fq "virtual ~Base() = default;" "$repo_root/build/inheritance_virtual_destructor.cpp"
grep -Fq "virtual ~Derived() = default;" "$repo_root/build/inheritance_virtual_destructor.cpp"
"$repo_root/build/dudu" "$repo_root/tests/fixtures/inheritance_virtual_drop.dd" \
    --emit-cpp "$repo_root/build/inheritance_virtual_drop.cpp"
grep -Fq "virtual ~Resource(" "$repo_root/build/inheritance_virtual_drop.cpp"
compile_and_expect native_inheritance_basic 42
compile_and_expect native_imported_base 62
compile_and_expect native_template_function 42
compile_and_expect native_scan_local 42

"$repo_root/build/duc" check "$repo_root/tests/fixtures/simple_program.dd"
"$repo_root/build/duc" run "$repo_root/tests/fixtures/value_match.dd"
"$repo_root/build/dudu" check "$repo_root/tests/fixtures/project_import_metadata/main.dd"
"$repo_root/build/dudu" --version | grep -q '^dudu 0\.1\.0$'
"$repo_root/build/dudu" --help | grep -Fq 'dudu build [input.dd|target] [--quiet] [--verbose]'
"$repo_root/build/dudu" --help | grep -Fq 'dudu check [input.dd|dir] [--quiet]'
"$repo_root/build/dudu" bench compiler --quiet -- --help | grep -q 'bench_compiler.sh'
direct_smoke="$repo_root/build/direct_backend_smoke"
rm -rf "$direct_smoke"
mkdir -p "$direct_smoke"
cp "$repo_root/tests/fixtures/simple_program.dd" "$direct_smoke/main.dd"
cat >"$direct_smoke/dudu.toml" <<'TOML'
name = "direct_output_smoke"
entry = "main.dd"
build_dir = "."

[build]
backend = "direct"
TOML
direct_build_output="$(
    cd "$direct_smoke"
    "$repo_root/build/dudu" build -o "$repo_root/build/direct_output_smoke" 2>&1
)"
printf '%s\n' "$direct_build_output" | grep -Eq '^backend direct$'
printf '%s\n' "$direct_build_output" | grep -Eq '^entry .*/main\.dd$'
printf '%s\n' "$direct_build_output" | grep -Eq '^analyze .*/main\.dd$'
printf '%s\n' "$direct_build_output" | grep -Eq '^emit .*/direct_output_smoke\.cpp$'
printf '%s\n' "$direct_build_output" | grep -Eq '^compile .*/direct_output_smoke$'
printf '%s\n' "$direct_build_output" | grep -Eq '^output .*/direct_output_smoke$'
cmake_build_output="$("$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/project_backend_cmake" 2>&1)"
printf '%s\n' "$cmake_build_output" | grep -Eq '^backend cmake$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^cmake .*/build/project_backend_cmake/cmake-backend/source/CMakeLists\.txt$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^generate .*/build/project_backend_cmake/cmake-backend/source/CMakeLists\.txt$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^configure .*/build/project_backend_cmake/cmake-backend/build$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^compile .*/build/project_backend_cmake/cmake-backend/build$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^output .*/backend_cmake$'
cmake_cached_output="$("$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/project_backend_cmake" 2>&1)"
printf '%s\n' "$cmake_cached_output" | grep -Eq '^configure .*/build/project_backend_cmake/cmake-backend/build$'
if printf '%s\n' "$cmake_cached_output" | grep -q '^-- Configuring done'; then
    echo "cached generated-CMake build reran configure" >&2
    exit 1
fi
"$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/project_backend_cmake_function_namespaces" --quiet
"$repo_root/build/duc" fmt "$repo_root/tests/fixtures/simple_program.dd" --check
"$repo_root/scripts/test_lsp.sh"

echo "fast compiler checks passed"
