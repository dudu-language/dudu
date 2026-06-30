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
compile_and_expect c_import_alias 42
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
compile_and_expect reference_array_index 42
compile_and_expect array_row_index 7
compile_and_expect array_c_handoff 42
compile_and_expect array_slice_view 42
compile_and_expect array_open_slice 42
compile_and_expect array_row_slice 42
compile_and_expect array_column_slice 42
compile_and_expect array_channel_slice 45
compile_and_expect array_full_matrix_slice 42
compile_and_expect array_trailing_range_slice 21
compile_and_expect array_matrix_patch_slice 18
compile_and_expect strided_span2_reslice 70
compile_and_expect array_matrix_row_range_slice 57
compile_and_expect array_volume_slab_slice 100
compile_and_expect array_volume_literal 42
compile_and_expect array_step_slice 42
compile_and_expect array_image_kernel 42
compile_and_expect generic_full_matrix_slice 42
compile_and_expect generic_column_slice 27
compile_and_expect generic_channel_slice 66
compile_and_expect generic_trailing_range_slice 90
compile_and_expect member_full_matrix_slice 42
compile_and_expect member_column_slice 39
compile_and_expect member_channel_slice 45
compile_and_expect member_trailing_range_slice 210
compile_and_expect swizzle_vec2 42
compile_and_expect swizzle_rgba 42
compile_and_expect swizzle_stpq 42
compile_and_expect swizzle_expr_receiver 42
compile_and_expect swizzle_width_result 42
compile_and_expect cpp_swizzle 42
compile_and_expect swizzle_assignment 42
compile_and_expect matrix_math 26
compile_and_expect cpu_tensor_matmul 42
compile_and_expect tensor_index_hook 42
compile_and_expect tensor_index_set_hook 42
compile_and_expect tensor_index_set_member_hook 42
compile_and_expect tensor_multi_index_hook 42
compile_and_expect tensor_slice_hook 42
compile_and_expect tensor_slice_views 42
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
compile_and_expect native_enum_compare 42
compile_and_expect native_template_function 42
compile_and_expect native_scan_local 42

"$repo_root/build/duc" check "$repo_root/tests/fixtures/simple_program.dd"
"$repo_root/build/duc" run "$repo_root/tests/fixtures/value_match.dd" \
    -o "$repo_root/build/duc_run_value_match"
compile_and_expect value_match_string 42
compile_and_expect value_match_assign 60
"$repo_root/build/dudu" check "$repo_root/tests/fixtures/project_import_metadata/main.dd"
"$repo_root/build/dudu" --version | grep -q '^dudu 0\.1\.0$'
"$repo_root/build/dudu" --help | grep -Fq 'dudu build [input.dd|target] [--quiet] [--verbose]'
"$repo_root/build/dudu" --help | grep -Fq 'dudu check [input.dd|dir] [--quiet] [--timings]'
"$repo_root/build/dudu" bench compiler --quiet -- --help | grep -q 'bench_compiler.sh'
init_parent="$(mktemp -d)"
init_smoke="$init_parent/hello_init"
"$repo_root/build/dudu" init "$init_smoke"
test -f "$init_smoke/dudu.toml"
test -f "$init_smoke/src/main.dd"
test -f "$init_smoke/README.md"
test -f "$init_smoke/CMakeLists.txt"
grep -Fq 'file(GLOB_RECURSE DUDU_SOURCES CONFIGURE_DEPENDS' "$init_smoke/CMakeLists.txt"
grep -Fq 'file(GLOB_RECURSE DUDU_GENERATED_SOURCES CONFIGURE_DEPENDS' "$init_smoke/CMakeLists.txt"
grep -Fq 'OUTPUT "${DUDU_GENERATED_STAMP}"' "$init_smoke/CMakeLists.txt"
grep -Fq 'BYPRODUCTS ${DUDU_GENERATED_SOURCES}' "$init_smoke/CMakeLists.txt"
grep -Fq 'DEPENDS "${DUC_EXECUTABLE}" "${DUDU_MANIFEST}" ${DUDU_SOURCES}' "$init_smoke/CMakeLists.txt"
test -f "$init_smoke/.gitignore"
test -d "$init_smoke/.git"
init_run_output="$(
    cd "$init_smoke"
    "$repo_root/build/dudu" run --quiet
)"
printf '%s\n' "$init_run_output" | grep -Fq 'hello from dudu'
cmake -S "$init_smoke" -B "$init_smoke/cmake-build" \
    -DDUC_EXECUTABLE="$repo_root/build/duc" >/dev/null
cmake --build "$init_smoke/cmake-build" >/dev/null
cmake_init_output="$("$init_smoke/cmake-build/hello_init")"
printf '%s\n' "$cmake_init_output" | grep -Fq 'hello from dudu'
cat >>"$init_smoke/dudu.toml" <<'TOML'

[cmake]
source = "."
target = "hello_init"
TOML
user_cmake_init_output="$(
    cd "$init_smoke"
    PATH="$repo_root/build:$PATH" "$repo_root/build/dudu" run --quiet
)"
printf '%s\n' "$user_cmake_init_output" | grep -Fq 'hello from dudu'
rm -rf "$init_parent"
new_smoke="$repo_root/build/project_new_smoke"
rm -rf "$new_smoke"
"$repo_root/build/dudu" new "$new_smoke"
test -f "$new_smoke/dudu.toml"
test -f "$new_smoke/src/main.dd"
test -f "$new_smoke/CMakeLists.txt"
grep -Fq 'file(GLOB_RECURSE DUDU_SOURCES CONFIGURE_DEPENDS' "$new_smoke/CMakeLists.txt"
grep -Fq 'file(GLOB_RECURSE DUDU_GENERATED_SOURCES CONFIGURE_DEPENDS' "$new_smoke/CMakeLists.txt"
grep -Fq 'OUTPUT "${DUDU_GENERATED_STAMP}"' "$new_smoke/CMakeLists.txt"
grep -Fq 'BYPRODUCTS ${DUDU_GENERATED_SOURCES}' "$new_smoke/CMakeLists.txt"
test ! -e "$new_smoke/.gitignore"
new_run_output="$(
    cd "$new_smoke"
    "$repo_root/build/dudu" run --quiet
)"
printf '%s\n' "$new_run_output" | grep -Fq 'hello from dudu'
emit_timing_dir="$repo_root/build/emit_modules_timing_smoke"
rm -rf "$emit_timing_dir"
emit_timing_output="$("$repo_root/build/duc" emit-modules \
    "$repo_root/tests/fixtures/project_import_metadata/main.dd" \
    -o "$emit_timing_dir" --timings 2>&1)"
printf '%s\n' "$emit_timing_output" | grep -Eq '^\[\+[0-9]+\.[0-9]{3}s\] parse '
printf '%s\n' "$emit_timing_output" | grep -Eq '^\[\+[0-9]+\.[0-9]{3}s\] indexed '
printf '%s\n' "$emit_timing_output" | grep -Eq '^\[\+[0-9]+\.[0-9]{3}s\] dirty '
printf '%s\n' "$emit_timing_output" | grep -Eq '^\[\+[0-9]+\.[0-9]{3}s\] analyze '
printf '%s\n' "$emit_timing_output" | grep -Eq '^\[\+[0-9]+\.[0-9]{3}s\] emit '
emit_timing_cached_output="$("$repo_root/build/duc" emit-modules \
    "$repo_root/tests/fixtures/project_import_metadata/main.dd" \
    -o "$emit_timing_dir" --timings 2>&1)"
printf '%s\n' "$emit_timing_cached_output" | grep -Fq 'dirty 0 modules'
printf '%s\n' "$emit_timing_cached_output" | grep -Fq 'analyze 0 modules'
if printf '%s\n' "$emit_timing_cached_output" | grep -Eq ' load | parse | indexed '; then
    echo "cached emit-modules unexpectedly loaded the module graph" >&2
    exit 1
fi
rm "$emit_timing_dir/camera.cpp"
emit_timing_missing_artifact_output="$("$repo_root/build/duc" emit-modules \
    "$repo_root/tests/fixtures/project_import_metadata/main.dd" \
    -o "$emit_timing_dir" --timings 2>&1)"
printf '%s\n' "$emit_timing_missing_artifact_output" | grep -Eq '^\[\+[0-9]+\.[0-9]{3}s\] load '
test -f "$emit_timing_dir/camera.cpp"
emit_incremental_project="$repo_root/build/emit_modules_incremental_smoke/project"
emit_incremental_dir="$repo_root/build/emit_modules_incremental_smoke/generated"
rm -rf "$repo_root/build/emit_modules_incremental_smoke"
mkdir -p "$emit_incremental_project"
cp -R "$repo_root/tests/fixtures/project_import_metadata/." "$emit_incremental_project/"
"$repo_root/build/duc" emit-modules "$emit_incremental_project/main.dd" \
    -o "$emit_incremental_dir" >/dev/null
sleep 1
windowing_before="$(stat -c %y "$emit_incremental_dir/windowing.cpp")"
camera_before="$(stat -c %y "$emit_incremental_dir/camera.cpp")"
renderer_before="$(stat -c %y "$emit_incremental_dir/renderer.cpp")"
vec3_before="$(stat -c %y "$emit_incremental_dir/vec3.cpp")"
perl -pi -e 's/height=22/height=23/' "$emit_incremental_project/windowing.dd"
emit_incremental_output="$("$repo_root/build/duc" emit-modules \
    "$emit_incremental_project/main.dd" -o "$emit_incremental_dir" --timings 2>&1)"
printf '%s\n' "$emit_incremental_output" | grep -Fq 'dirty 2 modules'
printf '%s\n' "$emit_incremental_output" | grep -Fq 'analyze 2 modules'
windowing_after="$(stat -c %y "$emit_incremental_dir/windowing.cpp")"
camera_after="$(stat -c %y "$emit_incremental_dir/camera.cpp")"
renderer_after="$(stat -c %y "$emit_incremental_dir/renderer.cpp")"
vec3_after="$(stat -c %y "$emit_incremental_dir/vec3.cpp")"
if [[ "$windowing_before" == "$windowing_after" ]]; then
    echo "changed module artifact was not rewritten" >&2
    exit 1
fi
if [[ "$camera_before" != "$camera_after" || "$renderer_before" != "$renderer_after" || \
      "$vec3_before" != "$vec3_after" ]]; then
    echo "unaffected module artifacts were rewritten" >&2
    exit 1
fi
rejected_direct_backend="$repo_root/build/rejected_direct_backend_smoke"
rm -rf "$rejected_direct_backend"
mkdir -p "$rejected_direct_backend"
cp "$repo_root/tests/fixtures/simple_program.dd" "$rejected_direct_backend/main.dd"
cat >"$rejected_direct_backend/dudu.toml" <<'TOML'
name = "rejected_direct_backend"
entry = "main.dd"
build_dir = "."

[build]
backend = "direct"
TOML
if (
    cd "$rejected_direct_backend"
    "$repo_root/build/dudu" build -o "$repo_root/build/rejected_direct_backend" \
        >"$repo_root/build/rejected_direct_backend.out" 2>"$repo_root/build/rejected_direct_backend.err"
); then
    echo "dudu project build unexpectedly accepted [build] backend = direct" >&2
    exit 1
fi
grep -Fq '[build] backend was removed' "$repo_root/build/rejected_direct_backend.err"
cmake_build_output="$("$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/project_backend_cmake" 2>&1)"
printf '%s\n' "$cmake_build_output" | grep -Eq '^backend cmake$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^cmake .*/build/project_backend_cmake/cmake-backend/source/CMakeLists\.txt$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^generate .*/build/project_backend_cmake/cmake-backend/source/CMakeLists\.txt$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^configure .*/build/project_backend_cmake/cmake-backend/build$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^compile .*/build/project_backend_cmake/cmake-backend/build$'
printf '%s\n' "$cmake_build_output" | grep -Eq '^output .*/backend_cmake$'
cmake_noop_output="$("$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/project_backend_cmake" 2>&1)"
printf '%s\n' "$cmake_noop_output" | grep -Eq 'Built target backend_cmake_dudu_generate'
if printf '%s\n' "$cmake_noop_output" | grep -Fq 'Dudu emit modules'; then
    echo "CMake no-op build unexpectedly re-ran Dudu emit modules" >&2
    exit 1
fi
cmake_cached_output="$("$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/project_backend_cmake" 2>&1)"
printf '%s\n' "$cmake_cached_output" | grep -Eq '^configure .*/build/project_backend_cmake/cmake-backend/build$'
if printf '%s\n' "$cmake_cached_output" | grep -q '^-- Configuring done'; then
    echo "cached generated-CMake build reran configure" >&2
    exit 1
fi
default_cmake_output="$("$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/simple_program.dd" 2>&1)"
printf '%s\n' "$default_cmake_output" | grep -Eq '^backend cmake$'
printf '%s\n' "$default_cmake_output" | grep -Eq '^output .*/simple_program$'
default_cmake_o="$repo_root/build/default_cmake_o_smoke/simple_program_copy"
rm -rf "$repo_root/build/default_cmake_o_smoke"
default_cmake_o_output="$("$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/simple_program.dd" -o "$default_cmake_o" 2>&1)"
printf '%s\n' "$default_cmake_o_output" | grep -Eq '^backend cmake$'
printf '%s\n' "$default_cmake_o_output" | grep -Eq "^output $default_cmake_o$"
test -x "$default_cmake_o"
set +e
"$default_cmake_o"
default_cmake_o_status=$?
set -e
if [[ "$default_cmake_o_status" -ne 42 ]]; then
    echo "generated-CMake -o copied binary returned $default_cmake_o_status, expected 42" >&2
    exit 1
fi
"$repo_root/build/dudu" build \
    "$repo_root/tests/fixtures/project_backend_cmake_function_namespaces" --quiet
"$repo_root/build/duc" fmt "$repo_root/tests/fixtures/simple_program.dd" --check
"$repo_root/scripts/test_lsp.sh"

echo "fast compiler checks passed"
