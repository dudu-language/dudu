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
"$repo_root/build/duc" emit "$repo_root/examples/raylib_game.dd" \
    -o "$repo_root/build/raylib_game_semantics.cpp"
grep -q "player.vel.x" "$repo_root/build/raylib_game_semantics.cpp"

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

test -f "$repo_root/editors/vscode/extension.js"
test -f "$repo_root/editors/vscode/syntaxes/dudu.tmLanguage.json"
test -f "$repo_root/editors/vim/syntax/dudu.vim"
test -f "$repo_root/editors/nvim/queries/dudu/highlights.scm"
grep -q '"command": "dudu.fmtFile"' "$repo_root/editors/vscode/package.json"
grep -q '"command": "dudu.checkFile"' "$repo_root/editors/vscode/package.json"
grep -q '"command": "dudu.buildProject"' "$repo_root/editors/vscode/package.json"
grep -q '"command": "dudu.runFile"' "$repo_root/editors/vscode/package.json"
grep -q 'registerCommand("dudu.fmtFile"' "$repo_root/editors/vscode/extension.js"
grep -q 'registerCommand("dudu.checkFile"' "$repo_root/editors/vscode/extension.js"
grep -q 'registerCommand("dudu.buildProject"' "$repo_root/editors/vscode/extension.js"
grep -q 'registerCommand("dudu.runFile"' "$repo_root/editors/vscode/extension.js"

generated_header="$repo_root/build/cpp_library.hpp"
"$repo_root/build/dudu" "$repo_root/examples/cpp_library.dd" --emit-header "$generated_header"
grep -q 'inline constexpr std::string_view TARGET_KIND = "executable";' "$generated_header"
grep -q 'inline constexpr std::string_view TARGET_MODE = "hosted";' "$generated_header"
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
grep -q "#define DUDU_CUDA_GLOBAL" "$repo_root/build/cuda_kernel.cpp"
grep -q "DUDU_CUDA_GLOBAL void saxpy_kernel" "$repo_root/build/cuda_kernel.cpp"
grep -q "float\\* dev_x = nullptr;" "$repo_root/build/cuda_kernel.cpp"
grep -q "err = cuda::cudaMalloc" "$repo_root/build/cuda_kernel.cpp"
! grep -q "dudu::dudu" "$repo_root/build/cuda_kernel.cpp"
"$repo_root/build/duc" emit "$repo_root/examples/shader_compute.dd" \
    -o "$repo_root/build/shader_compute.cpp"
grep -q "#define DUDU_SHADER_COMPUTE" "$repo_root/build/shader_compute.cpp"
grep -q "#define DUDU_WORKGROUP_SIZE" "$repo_root/build/shader_compute.cpp"
grep -q "DUDU_SHADER_COMPUTE DUDU_WORKGROUP_SIZE(8, 8, 1) void blur_x" \
    "$repo_root/build/shader_compute.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/lambda_callback.dd" \
    -o "$repo_root/build/function_pointer.cpp"
grep -Fq "std::add_pointer_t<int32_t(int32_t)> callback" \
    "$repo_root/build/function_pointer.cpp"
! grep -q "std::function" "$repo_root/build/function_pointer.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/tuple_return.dd" \
    -o "$repo_root/build/tuple_return.cpp"
grep -Fq "dudu::Tuple2<int32_t, int32_t> divmod_i32" "$repo_root/build/tuple_return.cpp"
grep -Fq "return {value / divisor, value % divisor};" "$repo_root/build/tuple_return.cpp"
! grep -q "std::tuple" "$repo_root/build/tuple_return.cpp"
! grep -q "#include <tuple>" "$repo_root/build/tuple_return.cpp"
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
(
    cd "$repo_root/tests/fixtures/project_cuda_mode"
    "$repo_root/build/duc" check
)
(
    cd "$repo_root/tests/fixtures/project_shader_mode"
    "$repo_root/build/duc" check
)
grep -q "inline constexpr bool DEBUG = true;" "$repo_root/build/project_mode.cpp"
grep -q 'inline constexpr std::string_view TARGET_KIND = "executable";' \
    "$repo_root/build/project_mode.cpp"
grep -q 'inline constexpr std::string_view TARGET_MODE = "hosted";' \
    "$repo_root/build/project_mode.cpp"
grep -q 'if constexpr (build::DEBUG && build::TARGET_KIND == "executable" && build::TARGET_MODE == "hosted")' \
    "$repo_root/build/project_mode.cpp"
(
    cd "$repo_root/tests/fixtures/project_cc"
    "$repo_root/build/duc" build -o "$repo_root/build/project_cc_bin" --verbose \
        2>"$repo_root/build/project_cc_verbose.err"
)
grep -q "project_cc_bin.cpp" "$repo_root/build/project_cc_verbose.err"
grep -q -- "-Iinclude" "$repo_root/build/project_cc_verbose.err"
grep -q -- "-DDUDU_PROJECT_CC=40" "$repo_root/build/project_cc_verbose.err"
grep -q -- "-DDUDU_PROJECT_CC_FLAG=2" "$repo_root/build/project_cc_verbose.err"
grep -q -- "-Llib" "$repo_root/build/project_cc_verbose.err"
grep -q "project_cc_bin.cpp" "$repo_root/build/compile_commands.json"
grep -q -- "-Iinclude" "$repo_root/build/compile_commands.json"
grep -q -- "-DDUDU_PROJECT_CC=40" "$repo_root/build/compile_commands.json"
grep -q -- "-DDUDU_PROJECT_CC_FLAG=2" "$repo_root/build/compile_commands.json"
grep -q -- "-Llib" "$repo_root/build/compile_commands.json"
set +e
"$repo_root/build/project_cc_bin"
project_cc_status=$?
set -e
if [[ "$project_cc_status" -ne 42 ]]; then
    echo "project_cc returned $project_cc_status, expected 42" >&2
    exit 1
fi
fake_pkg_config="$repo_root/build/fake-pkg-config"
cat >"$fake_pkg_config" <<'SH'
#!/usr/bin/env bash
set -euo pipefail

if [[ "$*" != "--cflags --libs fixturelib" ]]; then
    echo "unexpected pkg-config args: $*" >&2
    exit 1
fi

printf '%s\n' '-Iinclude'
SH
chmod +x "$fake_pkg_config"
(
    cd "$repo_root/tests/fixtures/project_pkg_config"
    PKG_CONFIG="$fake_pkg_config" "$repo_root/build/duc" build \
        -o "$repo_root/build/project_pkg_config_bin" --verbose \
        2>"$repo_root/build/project_pkg_config_verbose.err"
)
grep -q -- "-Iinclude" "$repo_root/build/project_pkg_config_verbose.err"
grep -q "project_pkg_config_bin.cpp" "$repo_root/build/compile_commands.json"
grep -q -- "-Iinclude" "$repo_root/build/compile_commands.json"
set +e
"$repo_root/build/project_pkg_config_bin"
project_pkg_config_status=$?
set -e
if [[ "$project_pkg_config_status" -ne 42 ]]; then
    echo "project_pkg_config returned $project_pkg_config_status, expected 42" >&2
    exit 1
fi
(
    cd "$repo_root/tests/fixtures/project_library"
    "$repo_root/build/duc" build -o "$repo_root/build/libproject_library.a" --verbose \
        2>"$repo_root/build/project_library_verbose.err"
)
test -f "$repo_root/build/libproject_library.a"
ar t "$repo_root/build/libproject_library.a" | grep -q "libproject_library.a.o"
grep -q -- "-c .*libproject_library.a.cpp" "$repo_root/build/project_library_verbose.err"
grep -q "ar rcs .*libproject_library.a" "$repo_root/build/project_library_verbose.err"
(
    cd "$repo_root/tests/fixtures/project_library"
    "$repo_root/build/duc" main.dd --emit-header "$repo_root/build/project_library.hpp"
)
printf '#include "project_library.hpp"\nint main() { return answer(); }\n' \
    >"$repo_root/build/project_library_caller.cpp"
"${CXX:-c++}" -std=c++20 -I"$repo_root/build" "$repo_root/build/project_library_caller.cpp" \
    "$repo_root/build/libproject_library.a" -o "$repo_root/build/project_library_caller"
set +e
"$repo_root/build/project_library_caller"
project_library_status=$?
set -e
if [[ "$project_library_status" -ne 42 ]]; then
    echo "project_library_caller returned $project_library_status, expected 42" >&2
    exit 1
fi
(
    cd "$repo_root/tests/fixtures/project_shared_library"
    "$repo_root/build/duc" build -o "$repo_root/build/libproject_shared.so" --verbose \
        2>"$repo_root/build/project_shared_library_verbose.err"
    "$repo_root/build/duc" main.dd --emit-header "$repo_root/build/project_shared_library.hpp"
)
test -f "$repo_root/build/libproject_shared.so"
grep -q -- "-fPIC -shared" "$repo_root/build/project_shared_library_verbose.err"
printf '#include "project_shared_library.hpp"\nint main() { return answer(); }\n' \
    >"$repo_root/build/project_shared_library_caller.cpp"
"${CXX:-c++}" -std=c++20 -I"$repo_root/build" \
    "$repo_root/build/project_shared_library_caller.cpp" \
    "$repo_root/build/libproject_shared.so" -Wl,-rpath,"$repo_root/build" \
    -o "$repo_root/build/project_shared_library_caller"
set +e
"$repo_root/build/project_shared_library_caller"
project_shared_library_status=$?
set -e
if [[ "$project_shared_library_status" -ne 42 ]]; then
    echo "project_shared_library_caller returned $project_shared_library_status, expected 42" >&2
    exit 1
fi
if (
    cd "$repo_root/tests/fixtures/project_library"
    "$repo_root/build/duc" run -o "$repo_root/build/project_library_run"
) 2>"$repo_root/build/project_library_run.err"; then
    echo "project_library run unexpectedly passed" >&2
    exit 1
fi
grep -q "cannot run target kind: library" "$repo_root/build/project_library_run.err"

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
compile_and_expect compile_time_compare 42
compile_and_expect tuple_return 43
compile_and_expect type_aliases 42
compile_and_expect enums 42
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
compile_and_expect multiline_literals 42
compile_and_expect nested_containers 42
compile_and_expect list_append_named 42
compile_and_expect class_methods 42
compile_and_expect c_import_alias 42
compile_and_expect pointer_cast 42
compile_and_expect pointer_member 42
compile_and_expect nested_fields 42
compile_and_expect align_up 42
compile_and_expect loop_control 25
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
expect_fail bad_type_alias --check "unknown type alias target: MissingThing"
expect_fail bad_dict_literal --emit-cpp "cannot assign set"
expect_fail bad_enum_underlying --check "unknown enum underlying type: MissingType"
expect_fail bad_enum_duplicate --check "duplicate enum value: value"
expect_fail bad_enum_value_name --check "enum values must be snake_case: BadValue"
expect_fail bad_return --emit-cpp "return type mismatch: expected i32, got bool"
expect_fail bad_unknown_identifier --emit-cpp "unknown identifier: missing_value"
expect_fail bad_unknown_function --emit-cpp "unknown function: missing"
expect_fail bad_unknown_type --emit-cpp "unknown local type: MissingType"
expect_fail bad_tuple_destructure --emit-cpp "tuple destructuring count mismatch"
expect_fail bad_nested_field --emit-cpp "unknown field: outer.inner.missing"
expect_fail bad_naming --check "type names must be PascalCase: bad_type"
expect_fail bad_parameter_name --check "parameter names must be snake_case: Value"
expect_fail bad_local_name --emit-cpp "local names must be snake_case or ALL_CAPS: BadValue"
expect_fail bad_for_name --emit-cpp "local names must be snake_case or ALL_CAPS: BadValue"
expect_fail bad_missing_return --emit-cpp "missing return in function: bad"
expect_fail bad_constructor_field --emit-cpp "unknown constructor field: Point.z"
expect_fail bad_constructor_duplicate --emit-cpp "duplicate constructor field: x"
expect_fail bad_constructor_type --emit-cpp "constructor field Point.x expects i32, got bool"
expect_fail bad_constructor_positional_type --emit-cpp "constructor Point argument 1 expects i32, got bool"
expect_fail bad_result_ok_type --emit-cpp "return type mismatch: expected Result\\[i32, i32\\], got Ok\\[bool\\]"
expect_fail bad_result_err_type --emit-cpp "return type mismatch: expected Result\\[i32, i32\\], got Err\\[bool\\]"
expect_fail bad_void_return --emit-cpp "void function cannot return i32"
expect_fail bad_break_outside_loop --emit-cpp "break outside loop"
expect_fail bad_continue_outside_loop --emit-cpp "continue outside loop"
expect_fail bad_index_read_type --emit-cpp "cannot assign i32 to bool without an explicit cast"
expect_fail bad_index_non_container --emit-cpp "cannot index non-container: value"
expect_fail bad_build_flag --check "unknown build flag: build.NOPE"
expect_fail bad_implicit_cast --emit-cpp "cannot assign i32 to i64 without an explicit cast"
expect_fail bad_const_assignment --emit-cpp "cannot assign to constant: LIMIT"
expect_fail bad_raise --check "unsupported Python feature: exceptions"
expect_fail bad_yield --check "unsupported Python feature: generators"
expect_fail bad_eval --check "unsupported Python feature: dynamic execution"
expect_fail bad_return_local_address --emit-cpp "cannot let local address escape: value"
expect_fail bad_append_local_address --emit-cpp "cannot let local address escape: value"
expect_fail bad_static_assert --check "static_assert failed: (PIXELS == 65)"
expect_fail bad_static_compare --check "static_assert failed: (PIXELS < 64)"
expect_fail bad_call_arity --emit-cpp "function add expects 2 arguments, got 1"
expect_fail bad_call_type --emit-cpp "argument 1 for negate expects i32, got bool"
expect_fail bad_callback_lambda --emit-cpp "argument 2 for apply expects fn(i32) -> i32, got lambda"
expect_fail bad_fn_pointer_call --emit-cpp "function callback expects 1 arguments, got 0"
expect_fail bad_tuple_arity --check "tuple supports 1 to 8 elements, got 9"
expect_fail bad_function_decorator --check "unknown function decorator: @cuda.glboal"
expect_fail bad_class_decorator --check "unknown class decorator: @packd"

if (
    cd "$repo_root/tests/fixtures/bad_target_decorator_mode"
    "$repo_root/build/duc" check
) 2>"$repo_root/build/bad_target_decorator_mode.err"; then
    echo "bad_target_decorator_mode unexpectedly passed" >&2
    exit 1
fi
grep -q '@cuda.global requires \[target\] mode = "cuda"' \
    "$repo_root/build/bad_target_decorator_mode.err"

if "$repo_root/build/duc" build "$repo_root/tests/fixtures/bad_native_build.dd" \
    -o "$repo_root/build/bad_native_build" 2>"$repo_root/build/bad_native_build.err"; then
    echo "bad_native_build unexpectedly passed" >&2
    exit 1
fi
grep -q "C++ build failed" "$repo_root/build/bad_native_build.err"
grep -q "source: .*bad_native_build.cpp" "$repo_root/build/bad_native_build.err"
grep -q "command: " "$repo_root/build/bad_native_build.err"

if "$repo_root/build/duc" emit "$repo_root/tests/fixtures/bad_package_build/main.dd" \
    -o "$repo_root/build/bad_package_build.cpp" 2>"$repo_root/build/bad_package_build.err"; then
    echo "bad_package_build unexpectedly passed" >&2
    exit 1
fi
grep -q "invalid \\[build\\] entry" "$repo_root/build/bad_package_build.err"

if (
    cd "$repo_root/tests/fixtures/bad_project_target"
    "$repo_root/build/duc" check
) 2>"$repo_root/build/bad_project_target.err"; then
    echo "bad_project_target unexpectedly passed" >&2
    exit 1
fi
grep -q "invalid \\[target\\] mode" "$repo_root/build/bad_project_target.err"

if (
    cd "$repo_root/tests/fixtures/bad_project_unknown_key"
    "$repo_root/build/duc" check
) 2>"$repo_root/build/bad_project_unknown_key.err"; then
    echo "bad_project_unknown_key unexpectedly passed" >&2
    exit 1
fi
grep -q "unknown \\[cc\\] entry" "$repo_root/build/bad_project_unknown_key.err"

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
printf '#include "dudu_api.hpp"\nint main() { Point p = make_point(10, 22); return answer() + point_sum(p) + static_cast<int>(Status::ok) - 23; }\n' >"$api_caller"
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
