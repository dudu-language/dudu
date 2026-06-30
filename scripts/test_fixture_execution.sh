#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

compile_and_expect simple_program 42
compile_and_expect control_flow 55
compile_and_expect compile_time_basic 64
compile_and_expect compile_time_compare 42
compile_and_expect tuple_return 43
compile_and_expect type_aliases 42
compile_and_expect enums 42
compile_and_expect explicit_casts 42
compile_and_expect allocation 17
compile_and_expect arena_allocator 43
compile_and_expect containers 42
compile_and_expect cpp_template_interop 42; compile_and_expect cpp_move_unique_ptr 42; compile_and_expect cpp_filesystem_path 42; compile_and_expect cpp_chrono_timer 42
compile_and_expect cpp_template_member 42
compile_and_expect cpp_operator_overload 42
compile_and_expect cpp_nested_native 42
compile_and_expect dudu_operator_overload 42
compile_and_expect dudu_operator_overload_rhs 42
compile_and_expect cpp_overloaded_constructor 42
compile_and_expect cpp_digit_underscore_name 42
compile_and_expect debug_asserts 42
"$repo_root/build/dudu" "$repo_root/tests/fixtures/freestanding_debug_assert.dd" \
    --emit-cpp "$repo_root/build/freestanding_debug_assert.cpp" -DTARGET_MODE=freestanding
grep -Fq "assert(((value == 42)))" "$repo_root/build/freestanding_debug_assert.cpp"
! grep -Fq "runtime_error" "$repo_root/build/freestanding_debug_assert.cpp"
compile_and_expect cpp_exceptions 42
compile_and_expect std_vector_map_string 42
compile_and_expect cpp_stdlib_interop 42
compile_and_expect cpp_std_variant 42
compile_and_expect anonymous_variant 42
compile_and_expect value_match_string 42
compile_and_expect value_match_assign 60
compile_and_expect native_dependent_template_return 42
compile_and_expect generic_non_type_param 42
compile_and_expect layout_attrs 21
compile_and_expect atomic_volatile 44
compile_and_expect branch_return 1
compile_and_expect constructors 42
compile_and_expect class_lifecycle 42
compile_and_expect static_members 42
compile_and_expect static_fields 42
compile_and_expect static_class_method_alias 42
compile_and_expect static_generic_method_alias 42
compile_and_expect native_imported_base 62
compile_and_expect native_template_function 42
compile_and_expect native_scan_local 42
compile_and_expect constructor_comparison_arg 42; compile_and_expect native_escape 42
compile_and_expect result_option 42
compile_and_expect function_pointers 42
compile_and_expect function_attrs 42
compile_and_expect section_attrs 42
compile_and_expect extern_c_handler 42
compile_and_expect cpp_namespace_alias 42
compile_and_expect fixed_arrays 42
compile_and_expect array_full_matrix_slice 42
compile_and_expect array_trailing_range_slice 21
compile_and_expect array_matrix_patch_slice 18
compile_and_expect strided_span2_reslice 70
compile_and_expect array_matrix_row_range_slice 57
compile_and_expect array_volume_slab_slice 100
compile_and_expect array_volume_literal 42
compile_and_expect generic_full_matrix_slice 42
compile_and_expect generic_column_slice 27
compile_and_expect generic_channel_slice 66
compile_and_expect generic_trailing_range_slice 90
compile_and_expect member_full_matrix_slice 42
compile_and_expect member_column_slice 39
compile_and_expect member_channel_slice 45
compile_and_expect member_trailing_range_slice 210
compile_and_expect cpu_tensor_matmul 42
compile_and_expect compound_assignment 46
compile_and_expect bitwise_ops 42
compile_and_expect binary_packet_parser 42
compile_and_expect ref_field_inference 42
compile_and_expect const_ref_field 42
compile_and_expect conditional_str 42
compile_and_expect comparison_call_args 42
compile_and_expect named_callback 42
compile_and_expect multiline_literals 42
compile_and_expect nested_containers 42
compile_and_expect list_append_named 42
compile_and_expect value_pointer_containers 42
compile_and_expect class_methods 42
compile_and_expect tensor_index_set_member_hook 42
compile_and_expect tensor_multi_index_hook 42
compile_and_expect tensor_slice_hook 42
compile_and_expect tensor_slice_views 42
compile_and_expect c_direct_lowercase_macro 42
compile_and_expect c_lowercase_macro 42
compile_and_expect c_variadic_macro 42
compile_and_expect cpp_macro_bomb 42
compile_and_expect c_import_alias 42; compile_and_expect c_macro_constants 42; compile_and_expect stdio_math 42; compile_and_expect c_qsort_callback 24; compile_and_expect c_struct_layout 42
compile_and_expect c_audio_callback 26
compile_and_expect pointer_cast 42
compile_and_expect pointer_member 42
compile_and_expect nested_fields 42
compile_and_expect align_up 42
compile_and_expect loop_control 25
compile_and_expect posix_mmap_hash 42
compile_and_expect posix_threads_mutex 42
compile_path_and_expect multifile tests/fixtures/multifile/main.dd 42

cmake_bin="$repo_root/build/dudu_build_simple"
"$repo_root/build/duc" build "$repo_root/tests/fixtures/simple_program.dd" -o "$cmake_bin"
set +e
"$cmake_bin"
cmake_status=$?
set -e
if [[ "$cmake_status" -ne 42 ]]; then
    echo "dudu build simple_program returned $cmake_status, expected 42" >&2
    exit 1
fi

"$repo_root/scripts/test_negative.sh"

api_cpp="$repo_root/build/dudu_api.cpp"
api_hpp="$repo_root/build/dudu_api.hpp"
api_caller="$repo_root/build/dudu_api_caller.cpp"
api_bin="$repo_root/build/dudu_api_caller"
"$repo_root/build/dudu" "$repo_root/tests/fixtures/dudu_api.dd" --emit-cpp "$api_cpp"
"$repo_root/build/dudu" "$repo_root/tests/fixtures/dudu_api.dd" --emit-header "$api_hpp"
grep -q "enum class Status" "$api_hpp"
grep -q "struct Point" "$api_hpp"
grep -q "inline constexpr int32_t ANSWER = 33;" "$api_hpp"
grep -q "Point make_point" "$api_hpp"
grep -q "private:" "$api_hpp"
grep -q "int32_t _secret()" "$api_hpp"
grep -q "int32_t sum()" "$api_hpp"
printf '#include "dudu_api.hpp"\nint main() { Point p = make_point(10, 22); return answer() + point_sum(p) + p.sum() + static_cast<int>(Status::Ok) - 43; }\n' >"$api_caller"
"${CXX:-c++}" -std=c++20 -I"$repo_root/build" "$api_cpp" "$api_caller" -o "$api_bin"
set +e
"$api_bin"
api_status=$?
set -e
if [[ "$api_status" -ne 42 ]]; then
    echo "dudu_api_caller returned $api_status, expected 42" >&2
    exit 1
fi

echo "compiler builds and canonical examples are present"
