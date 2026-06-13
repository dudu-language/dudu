#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"
"$repo_root/scripts/build.sh"
ctest --test-dir "$repo_root/build" --output-on-failure

required_examples=(
    allocators.dd
    audio_synth.dd
    compile_time.dd
    cpp_library.dd
    cuda_kernel.dd
    cuda_shared_memory_tile.dd
    ffmpeg_probe_decode.dd
    function_pointers.dd
    glfw_opengl_triangle.dd
    image_filter.dd
    interrupt_handler.dd
    layout_hardware.dd
    macro_bomb.dd
    modules_visibility.dd
    native_escape.dd
    numerics_kmeans.dd
    opencl_kernel_host.dd
    raylib_game.dd
    sdl3_window.dd
    shader_compute.dd
    systems_mmap.dd
    threading_atomics.dd
    vulkan_triangle.dd
    web_server.dd
)

for example in "${required_examples[@]}"; do
    test -f "$repo_root/examples/$example"
    "$repo_root/build/dudu" "$repo_root/examples/$example" --check
done
"$repo_root/build/duc" emit "$repo_root/examples/raylib_game.dd" \
    -o "$repo_root/build/raylib_game_semantics.cpp"
grep -q "player.vel.x" "$repo_root/build/raylib_game_semantics.cpp"

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
grep -q 'registerCompletionItemProvider("dudu"' "$repo_root/editors/vscode/extension.js"
grep -q 'createDiagnosticCollection("dudu")' "$repo_root/editors/vscode/extension.js"
grep -q '"onLanguage:dudu"' "$repo_root/editors/vscode/package.json"
grep -q '"duc"' "$repo_root/editors/vscode/extension.js"
grep -q '"emit"' "$repo_root/editors/vscode/extension.js"
grep -q "onDidSaveTextDocument" "$repo_root/editors/vscode/extension.js"
node --check "$repo_root/editors/vscode/extension.js"

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
"$repo_root/scripts/test_c_api.sh"
"$repo_root/scripts/test_dynamic_library.sh"
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
"$repo_root/build/duc" fmt "$repo_root/examples" --check
"$repo_root/build/duc" run "$repo_root/tests/fixtures/run_zero.dd" \
    -o "$repo_root/build/duc_run_zero"
clean_smoke="$repo_root/build/clean_smoke"
rm -rf "$clean_smoke"
"$repo_root/build/dudu" new "$clean_smoke" >/dev/null
(
    cd "$clean_smoke"
    "$repo_root/build/dudu" run >/dev/null 2>"$repo_root/build/dudu_run_steps.err"
    test -d build
    "$repo_root/build/dudu" clean 2>"$repo_root/build/dudu_clean.err"
    test ! -e build
)
grep -q "emit build/clean_smoke.cpp" "$repo_root/build/dudu_run_steps.err"
grep -q "build build/clean_smoke" "$repo_root/build/dudu_run_steps.err"
grep -q "run build/clean_smoke" "$repo_root/build/dudu_run_steps.err"
grep -q "clean ./build" "$repo_root/build/dudu_clean.err"
"$repo_root/build/dudu" test "$repo_root/tests/fixtures/dudu_tests.dd" \
    >"$repo_root/build/dudu_tests.out" 2>"$repo_root/build/dudu_test_steps.err"
grep -q "3/3 tests passed" "$repo_root/build/dudu_tests.out"
grep -Eq "emit build/dudu-tests/dudu_tests-[0-9a-f]+\\.cpp" "$repo_root/build/dudu_test_steps.err"
grep -Eq "test build/dudu-tests/dudu_tests-[0-9a-f]+$" "$repo_root/build/dudu_test_steps.err"
"$repo_root/build/dudu" test "$repo_root/tests/fixtures/simple_program.dd" \
    >"$repo_root/build/dudu_tests_zero.out"
grep -q "running 0 tests" "$repo_root/build/dudu_tests_zero.out"
grep -q "test result: ok. 0 passed; 0 failed; 0 filtered out" \
    "$repo_root/build/dudu_tests_zero.out"
"$repo_root/build/dudu" test "$repo_root/tests/fixtures/dudu_tests.dd" --filter bool \
    >"$repo_root/build/dudu_tests_filter.out" 2>"$repo_root/build/dudu_test_filter_steps.err"
grep -q "1/1 tests passed" "$repo_root/build/dudu_tests_filter.out"
grep -q "ok bool_result" "$repo_root/build/dudu_tests_filter.out"
if grep -q "ok add_works" "$repo_root/build/dudu_tests_filter.out"; then
    echo "dudu test filter ran an unfiltered test" >&2
    exit 1
fi
unfiltered_test_binary=$(sed -n 's/^test //p' "$repo_root/build/dudu_test_steps.err")
filtered_test_binary=$(sed -n 's/^test //p' "$repo_root/build/dudu_test_filter_steps.err")
if [ "$unfiltered_test_binary" = "$filtered_test_binary" ]; then
    echo "dudu test reused the unfiltered binary path for a filtered test" >&2
    exit 1
fi
cat >"$repo_root/build/dudu_failing_test.dd" <<'DD'
@test
def fails():
    assert 1 == 2
DD
if "$repo_root/build/dudu" test "$repo_root/build/dudu_failing_test.dd" \
    >"$repo_root/build/dudu_failing_test.out"; then
    echo "failing dudu test unexpectedly passed" >&2
    exit 1
fi
grep -q "FAILED fails: assert failed: 1 == 2" "$repo_root/build/dudu_failing_test.out"
cat >"$repo_root/build/dudu_assert_message_test.dd" <<'DD'
@test
def fails_with_message():
    assert 1 == 2, "numbers are still wrong"
DD
if "$repo_root/build/dudu" test "$repo_root/build/dudu_assert_message_test.dd" \
    >"$repo_root/build/dudu_assert_message_test.out"; then
    echo "dudu custom assert message test unexpectedly passed" >&2
    exit 1
fi
grep -q "FAILED fails_with_message: numbers are still wrong" \
    "$repo_root/build/dudu_assert_message_test.out"
cat >"$repo_root/build/dudu_test_decorators.dd" <<'DD'
@test.ignore
def slow_case():
    assert 1 == 2

@test.should_panic
def panics():
    assert 1 == 2

@test.should_panic("bad input")
def panics_with_message():
    assert 1 == 2, "bad input reached"
DD
"$repo_root/build/dudu" test "$repo_root/build/dudu_test_decorators.dd" \
    >"$repo_root/build/dudu_test_decorators.out"
grep -q "ignored slow_case" "$repo_root/build/dudu_test_decorators.out"
grep -q "ok panics" "$repo_root/build/dudu_test_decorators.out"
grep -q "ok panics_with_message" "$repo_root/build/dudu_test_decorators.out"
cat >"$repo_root/build/dudu_should_panic_fails.dd" <<'DD'
@test.should_panic
def does_not_panic():
    pass
DD
if "$repo_root/build/dudu" test "$repo_root/build/dudu_should_panic_fails.dd" \
    >"$repo_root/build/dudu_should_panic_fails.out"; then
    echo "dudu should_panic test unexpectedly passed without panic" >&2
    exit 1
fi
grep -q "FAILED does_not_panic: expected panic" \
    "$repo_root/build/dudu_should_panic_fails.out"
cat >"$repo_root/build/dudu_capture_test.dd" <<'DD'
@test
def passing_output():
    print("hidden line")

@test
def failing_output():
    print("visible failure line")
    assert 1 == 2
DD
"$repo_root/build/dudu" test "$repo_root/build/dudu_capture_test.dd" --filter passing \
    >"$repo_root/build/dudu_capture_pass.out"
grep -q "ok passing_output" "$repo_root/build/dudu_capture_pass.out"
if grep -q "hidden line" "$repo_root/build/dudu_capture_pass.out"; then
    echo "dudu test leaked captured output from passing test" >&2
    exit 1
fi
"$repo_root/build/dudu" test "$repo_root/build/dudu_capture_test.dd" --filter passing \
    --no-capture >"$repo_root/build/dudu_capture_nocapture.out"
grep -q "hidden line" "$repo_root/build/dudu_capture_nocapture.out"
if "$repo_root/build/dudu" test "$repo_root/build/dudu_capture_test.dd" --filter failing \
    >"$repo_root/build/dudu_capture_fail.out"; then
    echo "dudu capture failure test unexpectedly passed" >&2
    exit 1
fi
grep -q "visible failure line" "$repo_root/build/dudu_capture_fail.out"
grep -q "FAILED failing_output: assert failed: 1 == 2" \
    "$repo_root/build/dudu_capture_fail.out"
"$repo_root/scripts/test_codegen_shapes.sh"
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
    "$repo_root/build/duc" test -o "$repo_root/build/project_mode_tests"
)
(
    cd "$repo_root/tests/fixtures/project_targets"
    "$repo_root/build/dudu" check tool
    "$repo_root/build/dudu" build tool 2>"$repo_root/build/project_targets_build.err"
    test -x "$repo_root/build/project_targets/tool"
    "$repo_root/build/dudu" run tool >"$repo_root/build/project_targets_run.out" \
        2>"$repo_root/build/project_targets_run.err"
    "$repo_root/build/dudu" test >"$repo_root/build/project_targets_test.out" \
        2>"$repo_root/build/project_targets_test.err"
    "$repo_root/build/dudu" test ./... >"$repo_root/build/project_targets_recursive.out" \
        2>"$repo_root/build/project_targets_recursive.err"
    "$repo_root/build/dudu" test . >"$repo_root/build/project_targets_dir.out" \
        2>"$repo_root/build/project_targets_dir.err"
    "$repo_root/build/dudu" cmake tool -o "$repo_root/build/project_targets_cmake.txt" \
        2>"$repo_root/build/project_targets_cmake.err"
)
grep -q "build ../../../build/project_targets/tool" "$repo_root/build/project_targets_build.err"
grep -q "run ../../../build/project_targets/tool" "$repo_root/build/project_targets_run.err"
grep -q "tool target" "$repo_root/build/project_targets_run.out"
grep -q "ok target_test" "$repo_root/build/project_targets_test.out"
grep -q "1/1 tests passed" "$repo_root/build/project_targets_test.out"
grep -q "ok target_test" "$repo_root/build/project_targets_recursive.out"
grep -q "ok target_test" "$repo_root/build/project_targets_dir.out"
grep -q "add_executable(tool" "$repo_root/build/project_targets_cmake.txt"
grep -q 'set(DUDU_SOURCE "tool.dd")' "$repo_root/build/project_targets_cmake.txt"
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
grep -q "^c++ -std=c++20" "$repo_root/build/project_cc_verbose.err"
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
(
    cd "$repo_root/tests/fixtures/project_default_output"
    "$repo_root/build/duc" build --verbose 2>"$repo_root/build/project_default_output_verbose.err"
)
test -x "$repo_root/build/project_default_output/default_tool"
grep -q "project_default_output/default_tool.cpp" \
    "$repo_root/build/project_default_output_verbose.err"
set +e
"$repo_root/build/project_default_output/default_tool"
project_default_output_status=$?
set -e
if [[ "$project_default_output_status" -ne 42 ]]; then
    echo "project_default_output returned $project_default_output_status, expected 42" >&2
    exit 1
fi
(
    cd "$repo_root/tests/fixtures/project_linker_script"
    "$repo_root/build/duc" build -o "$repo_root/build/project_linker_script_bin" --verbose \
        2>"$repo_root/build/project_linker_script_verbose.err"
)
grep -q -- "-Wl,-T,linker.ld" "$repo_root/build/project_linker_script_verbose.err"
grep -q '__attribute__((section(".dudu_boot")))' \
    "$repo_root/build/project_linker_script_bin.cpp"
set +e
"$repo_root/build/project_linker_script_bin"
project_linker_script_status=$?
set -e
if [[ "$project_linker_script_status" -ne 42 ]]; then
    echo "project_linker_script returned $project_linker_script_status, expected 42" >&2
    exit 1
fi
cmake_project_dir="$repo_root/build/project_cmake"
rm -rf "$cmake_project_dir" "$repo_root/build/project_cmake_build"
mkdir -p "$cmake_project_dir"
"$repo_root/build/duc" cmake "$repo_root/tests/fixtures/project_default_output/main.dd" \
    -o "$cmake_project_dir/CMakeLists.txt"
grep -q "add_executable(default_tool" "$cmake_project_dir/CMakeLists.txt"
grep -q "DUDU_EXECUTABLE" "$cmake_project_dir/CMakeLists.txt"
cmake -S "$cmake_project_dir" -B "$repo_root/build/project_cmake_build" \
    -DDUDU_EXECUTABLE="$repo_root/build/duc" >/dev/null
cmake --build "$repo_root/build/project_cmake_build" >/dev/null
set +e
"$repo_root/build/project_cmake_build/default_tool"
project_cmake_status=$?
set -e
if [[ "$project_cmake_status" -ne 42 ]]; then
    echo "project_cmake returned $project_cmake_status, expected 42" >&2
    exit 1
fi
cmake_cc_dir="$repo_root/build/project_cmake_cc"
rm -rf "$cmake_cc_dir" "$repo_root/build/project_cmake_cc_build"
mkdir -p "$cmake_cc_dir"
"$repo_root/build/duc" cmake "$repo_root/tests/fixtures/project_cc/main.dd" \
    -o "$cmake_cc_dir/CMakeLists.txt"
grep -q "target_include_directories(main PRIVATE" "$cmake_cc_dir/CMakeLists.txt"
grep -q "target_compile_definitions(main PRIVATE \"DUDU_PROJECT_CC=40\")" \
    "$cmake_cc_dir/CMakeLists.txt"
grep -q "target_compile_options(main PRIVATE \"-DDUDU_PROJECT_CC_FLAG=2\")" \
    "$cmake_cc_dir/CMakeLists.txt"
grep -q "target_link_directories(main PRIVATE" "$cmake_cc_dir/CMakeLists.txt"
cmake -S "$cmake_cc_dir" -B "$repo_root/build/project_cmake_cc_build" \
    -DDUDU_EXECUTABLE="$repo_root/build/duc" >/dev/null
cmake --build "$repo_root/build/project_cmake_cc_build" >/dev/null
set +e
"$repo_root/build/project_cmake_cc_build/main"
project_cmake_cc_status=$?
set -e
if [[ "$project_cmake_cc_status" -ne 42 ]]; then
    echo "project_cmake_cc returned $project_cmake_cc_status, expected 42" >&2
    exit 1
fi
cmake_multifile_dir="$repo_root/build/project_cmake_multifile"
rm -rf "$cmake_multifile_dir" "$repo_root/build/project_cmake_multifile_build"
mkdir -p "$cmake_multifile_dir"
"$repo_root/build/duc" cmake "$repo_root/tests/fixtures/multifile/main.dd" \
    -o "$cmake_multifile_dir/CMakeLists.txt"
grep -q "helper.dd" "$cmake_multifile_dir/CMakeLists.txt"
cmake -S "$cmake_multifile_dir" -B "$repo_root/build/project_cmake_multifile_build" \
    -DDUDU_EXECUTABLE="$repo_root/build/duc" >/dev/null
cmake --build "$repo_root/build/project_cmake_multifile_build" >/dev/null
set +e
"$repo_root/build/project_cmake_multifile_build/main"
project_cmake_multifile_status=$?
set -e
if [[ "$project_cmake_multifile_status" -ne 42 ]]; then
    echo "project_cmake_multifile returned $project_cmake_multifile_status, expected 42" >&2
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
(
    cd "$repo_root/tests/fixtures/project_freestanding_mode"
    "$repo_root/build/duc" build -o "$repo_root/build/libproject_freestanding.a" --verbose \
        2>"$repo_root/build/project_freestanding_verbose.err"
)
test -f "$repo_root/build/libproject_freestanding.a"
grep -q -- "-ffreestanding" "$repo_root/build/project_freestanding_verbose.err"
grep -q -- "-fno-exceptions" "$repo_root/build/project_freestanding_verbose.err"
grep -q -- "-fno-rtti" "$repo_root/build/project_freestanding_verbose.err"
if grep -q "#include <iostream>" "$repo_root/build/libproject_freestanding.a.cpp"; then
    echo "freestanding prelude unexpectedly included iostream" >&2
    exit 1
fi
(
    cd "$repo_root/tests/fixtures/project_embedded_uart"
    "$repo_root/build/duc" build -o "$repo_root/build/libproject_embedded_uart.a" --verbose \
        2>"$repo_root/build/project_embedded_uart_verbose.err"
)
test -f "$repo_root/build/libproject_embedded_uart.a"
grep -q -- "-ffreestanding" "$repo_root/build/project_embedded_uart_verbose.err"
grep -q -- "-fno-exceptions" "$repo_root/build/project_embedded_uart_verbose.err"
grep -q -- "-fno-rtti" "$repo_root/build/project_embedded_uart_verbose.err"
grep -q "volatile uint32_t status" "$repo_root/build/libproject_embedded_uart.a.cpp"
grep -q "reinterpret_cast<volatile UartRegs\\*>" "$repo_root/build/libproject_embedded_uart.a.cpp"
if (
    cd "$repo_root/tests/fixtures/project_library"
    "$repo_root/build/duc" run -o "$repo_root/build/project_library_run"
) 2>"$repo_root/build/project_library_run.err"; then
    echo "project_library run unexpectedly passed" >&2
    exit 1
fi
grep -q "cannot run target kind: library" "$repo_root/build/project_library_run.err"

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
compile_and_expect cpp_overloaded_constructor 42
compile_and_expect cpp_digit_underscore_name 42
compile_and_expect debug_asserts 42
"$repo_root/build/dudu" "$repo_root/tests/fixtures/freestanding_debug_assert.dd" \
    --emit-cpp "$repo_root/build/freestanding_debug_assert.cpp" -DTARGET_MODE=freestanding
grep -Fq "assert((value == 42)" "$repo_root/build/freestanding_debug_assert.cpp"
! grep -Fq "runtime_error" "$repo_root/build/freestanding_debug_assert.cpp"
compile_and_expect cpp_exceptions 42
compile_and_expect std_vector_map_string 42
compile_and_expect layout_attrs 21
compile_and_expect atomic_volatile 44
compile_and_expect branch_return 1
compile_and_expect constructors 42
compile_and_expect class_lifecycle 42
compile_and_expect static_members 42
compile_and_expect constructor_comparison_arg 42; compile_and_expect native_escape 42
compile_and_expect result_option 42
compile_and_expect function_pointers 42
compile_and_expect function_attrs 42
compile_and_expect section_attrs 42
compile_and_expect extern_c_handler 42
compile_and_expect cpp_namespace_alias 42
compile_and_expect fixed_arrays 42
compile_and_expect compound_assignment 46
compile_and_expect bitwise_ops 42
compile_and_expect binary_packet_parser 42
compile_and_expect ref_field_inference 42
compile_and_expect const_ref_field 42
compile_and_expect conditional_str 42
compile_and_expect comparison_call_args 42
compile_and_expect lambda_callback 42
compile_and_expect multiline_literals 42
compile_and_expect nested_containers 42
compile_and_expect list_append_named 42
compile_and_expect value_pointer_containers 42
compile_and_expect class_methods 42
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
grep -q "int32_t secret()" "$api_hpp"
grep -q "int32_t sum()" "$api_hpp"
printf '#include "dudu_api.hpp"\nint main() { Point p = make_point(10, 22); return answer() + point_sum(p) + p.sum() + static_cast<int>(Status::ok) - 43; }\n' >"$api_caller"
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
