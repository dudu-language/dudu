#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"
ctest --test-dir "$repo_root/build" --output-on-failure

required_examples=(
    allocators.dd
    audio_synth.dd
    compile_time.dd
    cpp_library.dd
    cuda_kernel.dd
    function_pointers.dd
    image_filter.dd
    layout_hardware.dd
    modules_visibility.dd
    native_escape.dd
    numerics_kmeans.dd
    raylib_game.dd
    shader_compute.dd
    systems_mmap.dd
    threading_atomics.dd
    web_server.dd
)

for example in "${required_examples[@]}"; do
    test -f "$repo_root/examples/$example"
    "$repo_root/build/dudu" "$repo_root/examples/$example" --check
done

compile_example_object() {
    local example="$1"
    local name
    name="$(basename "$example" .dd)"
    local cpp="$repo_root/build/example_$name.cpp"
    local object="$repo_root/build/example_$name.o"

    "$repo_root/build/dudu" "$repo_root/examples/$example" --emit-cpp "$cpp"
    "${CXX:-c++}" -std=c++20 -c "$cpp" -o "$object"
}

object_examples=(
    allocators.dd
    compile_time.dd
    cpp_library.dd
    function_pointers.dd
    layout_hardware.dd
    modules_visibility.dd
    native_escape.dd
    numerics_kmeans.dd
    shader_compute.dd
    systems_mmap.dd
    threading_atomics.dd
)

for example in "${object_examples[@]}"; do
    compile_example_object "$example"
done

generated_header="$repo_root/build/cpp_library.hpp"
"$repo_root/build/dudu" "$repo_root/examples/cpp_library.dd" --emit-header "$generated_header"
printf '#include "cpp_library.hpp"\nint main() { return 0; }\n' >"$repo_root/build/header_smoke.cpp"
"${CXX:-c++}" -std=c++20 -I"$repo_root/build" -c "$repo_root/build/header_smoke.cpp" \
    -o "$repo_root/build/header_smoke.o"

"$repo_root/build/dudu" "$repo_root/tests/fixtures/simple_program.dd" --format - >/dev/null
"$repo_root/build/duc" --version | grep -q '^duc 0\.1\.0$'
"$repo_root/build/duc" "$repo_root/tests/fixtures/simple_program.dd" --check
"$repo_root/build/duc" check "$repo_root/tests/fixtures/simple_program.dd"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/simple_program.dd" \
    -o "$repo_root/build/duc_emit_simple.cpp"
grep -q "dudu: .*simple_program.dd:7:" "$repo_root/build/duc_emit_simple.cpp"
"$repo_root/build/duc" fmt "$repo_root/tests/fixtures/simple_program.dd" \
    -o "$repo_root/build/duc_fmt_simple.dd"
"$repo_root/build/duc" fmt "$repo_root/tests/fixtures/simple_program.dd" --check
if "$repo_root/build/duc" fmt "$repo_root/tests/fixtures/unformatted.dd" --check \
    2>"$repo_root/build/duc_fmt_check.err"; then
    echo "unformatted fixture unexpectedly passed format check" >&2
    exit 1
fi
grep -q "would reformat" "$repo_root/build/duc_fmt_check.err"
fmt_dir="$repo_root/build/fmt_dir"
rm -rf "$fmt_dir"
mkdir -p "$fmt_dir"
cp "$repo_root/tests/fixtures/unformatted.dd" "$fmt_dir/sample.dd"
"$repo_root/build/duc" fmt "$fmt_dir"
"$repo_root/build/duc" fmt "$fmt_dir" --check
"$repo_root/build/duc" run "$repo_root/tests/fixtures/run_zero.dd" \
    -o "$repo_root/build/duc_run_zero"
"$repo_root/build/dudu" "$repo_root/examples/compile_time.dd" --emit-cpp \
    "$repo_root/build/compile_time_raylib.cpp" -DDEBUG=true -DRENDER_BACKEND=raylib
grep -q "inline constexpr bool DEBUG = true;" "$repo_root/build/compile_time_raylib.cpp"
grep -q 'inline constexpr std::string_view RENDER_BACKEND = "raylib";' \
    "$repo_root/build/compile_time_raylib.cpp"
"$repo_root/build/duc" emit "$repo_root/examples/cuda_kernel.dd" \
    -o "$repo_root/build/cuda_kernel.cpp"
grep -q "float\\* dev_x = nullptr;" "$repo_root/build/cuda_kernel.cpp"
grep -q "err = cuda::cudaMalloc" "$repo_root/build/cuda_kernel.cpp"
! grep -q "dudu::dudu" "$repo_root/build/cuda_kernel.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/package_build/main.dd" \
    -o "$repo_root/build/package_build.cpp"
grep -q "inline constexpr bool DEBUG = true;" "$repo_root/build/package_build.cpp"
grep -q 'inline constexpr std::string_view RENDER_BACKEND = "raylib";' \
    "$repo_root/build/package_build.cpp"
grep -q "if constexpr (build::DEBUG && build::RENDER_BACKEND == \"raylib\")" \
    "$repo_root/build/package_build.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/package_build/main.dd" \
    -o "$repo_root/build/package_build_override.cpp" -DDEBUG=false
grep -q "inline constexpr bool DEBUG = false;" "$repo_root/build/package_build_override.cpp"
(
    cd "$repo_root/tests/fixtures/project_mode"
    "$repo_root/build/duc" check .
    "$repo_root/build/duc" check
    "$repo_root/build/duc" emit -o "$repo_root/build/project_mode.cpp"
    "$repo_root/build/duc" bench 1000
    "$repo_root/build/duc" test
)
grep -q "inline constexpr bool DEBUG = true;" "$repo_root/build/project_mode.cpp"
grep -q "if constexpr (build::DEBUG)" "$repo_root/build/project_mode.cpp"

compile_and_expect() {
    local name="$1"
    local expected="$2"
    local cpp="$repo_root/build/$name.cpp"
    local bin="$repo_root/build/$name"

    "$repo_root/build/dudu" "$repo_root/tests/fixtures/$name.dd" --emit-cpp "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne "$expected" ]]; then
        echo "$name returned $status, expected $expected" >&2
        exit 1
    fi
}

compile_path_and_expect() {
    local name="$1"
    local path="$2"
    local expected="$3"
    local cpp="$repo_root/build/$name.cpp"
    local bin="$repo_root/build/$name"

    "$repo_root/build/dudu" "$repo_root/$path" --emit-cpp "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne "$expected" ]]; then
        echo "$name returned $status, expected $expected" >&2
        exit 1
    fi
}

compile_and_expect simple_program 42
compile_and_expect control_flow 55
compile_and_expect compile_time_basic 64
compile_and_expect tuple_return 43
compile_and_expect allocation 17
compile_and_expect containers 42
compile_and_expect cpp_template_interop 42
compile_and_expect layout_attrs 21
compile_and_expect atomic_volatile 44
compile_and_expect branch_return 1
compile_and_expect constructors 42
compile_and_expect native_escape 42
compile_and_expect result_option 42
compile_and_expect function_attrs 42
compile_and_expect cpp_namespace_alias 42
compile_and_expect fixed_arrays 42
compile_and_expect compound_assignment 46
compile_and_expect ref_field_inference 42
compile_and_expect conditional_str 42
compile_and_expect lambda_callback 42
compile_and_expect list_append_named 42
compile_and_expect class_methods 42
compile_and_expect c_import_alias 42
compile_and_expect pointer_cast 42
compile_and_expect pointer_member 42
compile_and_expect align_up 42
compile_path_and_expect multifile tests/fixtures/multifile/main.dd 42

direct_bin="$repo_root/build/dudu_build_simple"
"$repo_root/build/dudu" build "$repo_root/tests/fixtures/simple_program.dd" -o "$direct_bin"
set +e
"$direct_bin"
direct_status=$?
set -e
if [[ "$direct_status" -ne 42 ]]; then
    echo "dudu build simple_program returned $direct_status, expected 42" >&2
    exit 1
fi

expect_fail() {
    local name="$1"
    local mode="$2"
    local expected="$3"
    local cmd=("$repo_root/build/dudu" "$repo_root/tests/fixtures/$name.dd" "$mode")
    if [[ "$mode" != "--check" ]]; then
        cmd+=("$repo_root/build/$name.out")
    fi
    if "${cmd[@]}" 2>"$repo_root/build/$name.err"; then
        echo "$name unexpectedly passed" >&2
        exit 1
    fi
    grep -q "$expected" "$repo_root/build/$name.err"
}

expect_fail bad_duplicate --check "duplicate declaration: Vec"
expect_fail bad_return --emit-cpp "return type mismatch: expected i32, got bool"
expect_fail bad_unknown_type --emit-cpp "unknown local type: MissingType"
expect_fail bad_tuple_destructure --emit-cpp "tuple destructuring count mismatch"
expect_fail bad_naming --check "type names must be PascalCase: bad_type"
expect_fail bad_missing_return --emit-cpp "missing return in function: bad"
expect_fail bad_build_flag --check "unknown build flag: build.NOPE"
expect_fail bad_implicit_cast --emit-cpp "cannot assign i32 to i64 without an explicit cast"
expect_fail bad_const_assignment --emit-cpp "cannot assign to constant: LIMIT"
expect_fail bad_raise --check "unsupported Python feature: exceptions"
expect_fail bad_yield --check "unsupported Python feature: generators"
expect_fail bad_eval --check "unsupported Python feature: dynamic execution"
expect_fail bad_return_local_address --emit-cpp "cannot let local address escape: value"
expect_fail bad_append_local_address --emit-cpp "cannot let local address escape: value"
expect_fail bad_static_assert --check "static_assert failed: (PIXELS == 65)"

if "$repo_root/build/duc" emit "$repo_root/tests/fixtures/bad_package_build/main.dd" \
    -o "$repo_root/build/bad_package_build.cpp" 2>"$repo_root/build/bad_package_build.err"; then
    echo "bad_package_build unexpectedly passed" >&2
    exit 1
fi
grep -q "invalid \\[build\\] entry" "$repo_root/build/bad_package_build.err"

api_cpp="$repo_root/build/dudu_api.cpp"
api_hpp="$repo_root/build/dudu_api.hpp"
api_caller="$repo_root/build/dudu_api_caller.cpp"
api_bin="$repo_root/build/dudu_api_caller"
"$repo_root/build/dudu" "$repo_root/tests/fixtures/dudu_api.dd" --emit-cpp "$api_cpp"
"$repo_root/build/dudu" "$repo_root/tests/fixtures/dudu_api.dd" --emit-header "$api_hpp"
printf '#include "dudu_api.hpp"\nint main() { return answer() + 9; }\n' >"$api_caller"
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
