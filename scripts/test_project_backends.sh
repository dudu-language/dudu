#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/package_build/main.dd" \
    -o "$repo_root/build/package_build.cpp"
grep -q "inline constexpr bool DEBUG = true;" "$repo_root/build/package_build.cpp"
grep -q 'inline constexpr std::string_view RENDER_BACKEND = "raylib";' \
    "$repo_root/build/package_build.cpp"
grep -q "if constexpr ((build::DEBUG && (build::RENDER_BACKEND == \"raylib\")))" \
    "$repo_root/build/package_build.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/package_build/main.dd" \
    -o "$repo_root/build/package_build_override.cpp" -DDEBUG=false
grep -q "inline constexpr bool DEBUG = false;" "$repo_root/build/package_build_override.cpp"
(
    cd "$repo_root/tests/fixtures/project_mode"
    "$repo_root/build/duc" check .
    "$repo_root/build/duc" check
    "$repo_root/build/dudu" check 2>"$repo_root/build/project_mode_check.err"
    "$repo_root/build/dudu" check --quiet 2>"$repo_root/build/project_mode_check_quiet.err"
    "$repo_root/build/duc" emit -o "$repo_root/build/project_mode.cpp"
    "$repo_root/build/duc" bench 1000
    "$repo_root/build/duc" test -o "$repo_root/build/project_mode_tests"
)
grep -q "check .*project_mode/main.dd" "$repo_root/build/project_mode_check.err"
grep -q "ok .*project_mode/main.dd" "$repo_root/build/project_mode_check.err"
test ! -s "$repo_root/build/project_mode_check_quiet.err"
"$repo_root/build/dudu" bench --help >"$repo_root/build/dudu_bench_help.out"
grep -q "dudu bench" "$repo_root/build/dudu_bench_help.out"
(
    cd "$repo_root/tests/fixtures/project_delegated_command/subdir"
    "$repo_root/build/dudu" bench >"$repo_root/build/project_delegated_bench.out"
    "$repo_root/build/dudu" bench -- --user-flag \
        >"$repo_root/build/project_delegated_bench_args.out"
    "$repo_root/build/duc" bench --quiet \
        >"$repo_root/build/project_delegated_duc_bench_args.out"
    "$repo_root/build/dudu" bench --quiet >"$repo_root/build/project_delegated_bench_quiet.out" \
        2>"$repo_root/build/project_delegated_bench_quiet.err"
    "$repo_root/build/dudu" test >"$repo_root/build/project_delegated_test.out"
)
grep -q "bench-from-project" "$repo_root/build/project_delegated_bench.out"
grep -q "bench-from-project --user-flag" "$repo_root/build/project_delegated_bench_args.out"
grep -q "bench-from-project --quiet" "$repo_root/build/project_delegated_duc_bench_args.out"
grep -q "bench-from-project" "$repo_root/build/project_delegated_bench_quiet.out"
test ! -s "$repo_root/build/project_delegated_bench_quiet.err"
grep -q "test-from-project" "$repo_root/build/project_delegated_test.out"
(
    cd "$repo_root/tests/fixtures/project_targets"
    "$repo_root/build/dudu" check tool
    "$repo_root/build/dudu" build tool 2>"$repo_root/build/project_targets_build.err"
    test -x "$repo_root/build/project_targets/cmake-backend/build/tool"
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
grep -Eq "backend cmake" "$repo_root/build/project_targets_build.err"
grep -Eq "compile .*project_targets/cmake-backend/build" "$repo_root/build/project_targets_build.err"
grep -Eq "output .*project_targets/cmake-backend/build/tool" "$repo_root/build/project_targets_build.err"
grep -Eq "run '.*project_targets/cmake-backend/build/tool'" "$repo_root/build/project_targets_run.err"
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
grep -q 'if constexpr (((build::DEBUG && (build::TARGET_KIND == "executable")) && (build::TARGET_MODE == "hosted")))' \
    "$repo_root/build/project_mode.cpp"
rm -f "$repo_root/build/project_cc_bin" "$repo_root/build/project_cc_bin.cpp"
(
    cd "$repo_root/tests/fixtures/project_cc"
    "$repo_root/build/duc" build -o "$repo_root/build/project_cc_bin" --verbose \
        2>"$repo_root/build/project_cc_verbose.err"
)
project_cc_cmake="$repo_root/tests/fixtures/project_cc/build/cmake-backend/source/CMakeLists.txt"
grep -q "target_include_directories(main PRIVATE" "$project_cc_cmake"
grep -Eq "project_cc/include" "$project_cc_cmake"
grep -q 'target_compile_definitions(main PRIVATE "DUDU_PROJECT_CC=40")' "$project_cc_cmake"
grep -q 'target_compile_options(main PRIVATE "-DDUDU_PROJECT_CC_FLAG=2")' "$project_cc_cmake"
grep -Eq "project_cc/lib" "$project_cc_cmake"
set +e
"$repo_root/build/project_cc_bin"
project_cc_status=$?
set -e
if [[ "$project_cc_status" -ne 42 ]]; then
    echo "project_cc returned $project_cc_status, expected 42" >&2
    exit 1
fi
rm -rf "$repo_root/build/project_default_output"
(
    cd "$repo_root/tests/fixtures/project_default_output"
    "$repo_root/build/duc" build --verbose 2>"$repo_root/build/project_default_output_verbose.err"
)
test -x "$repo_root/build/project_default_output/cmake-backend/build/default_tool"
grep -q "cmake .*build/project_default_output/cmake-backend/source" \
    "$repo_root/build/project_default_output_verbose.err"
grep -q "build .*build/project_default_output/cmake-backend/build" \
    "$repo_root/build/project_default_output_verbose.err"
set +e
"$repo_root/build/project_default_output/cmake-backend/build/default_tool"
project_default_output_status=$?
set -e
if [[ "$project_default_output_status" -ne 42 ]]; then
    echo "project_default_output returned $project_default_output_status, expected 42" >&2
    exit 1
fi
(
    cd "$repo_root/tests/fixtures/project_backend_cmake"
    "$repo_root/build/dudu" build --verbose 2>"$repo_root/build/project_backend_cmake_build.err"
    "$repo_root/build/dudu" run >"$repo_root/build/project_backend_cmake_run.out" \
        2>"$repo_root/build/project_backend_cmake_run.err"
    "$repo_root/build/dudu" test >"$repo_root/build/project_backend_cmake_test.out" \
        2>"$repo_root/build/project_backend_cmake_test.err"
)
grep -Eq "cmake .*project_backend_cmake/cmake-backend/source/CMakeLists.txt" \
    "$repo_root/build/project_backend_cmake_build.err"
grep -Eq "build .*project_backend_cmake/cmake-backend/build" \
    "$repo_root/build/project_backend_cmake_build.err"
grep -Eq "run .*project_backend_cmake/cmake-backend/build/backend_cmake" \
    "$repo_root/build/project_backend_cmake_run.err"
grep -q "cmake backend" "$repo_root/build/project_backend_cmake_run.out"
grep -q "42" "$repo_root/build/project_backend_cmake_run.out"
test -f "$repo_root/build/project_backend_cmake/cmake-backend/build/generated/main.cpp"
test -f "$repo_root/build/project_backend_cmake/cmake-backend/build/generated/helper.cpp"
grep -Eq "cmake .*project_backend_cmake/dudu-tests/main-[0-9a-f]+-cmake/source/CMakeLists.txt" \
    "$repo_root/build/project_backend_cmake_test.err"
grep -q "2/2 tests passed" "$repo_root/build/project_backend_cmake_test.out"
cmake_test_binary=$(sed -n 's/^test //p' "$repo_root/build/project_backend_cmake_test.err" | tail -1)
cmake_test_generated_dir="$(dirname "$cmake_test_binary")/generated"
test -f "$cmake_test_generated_dir/main.cpp"
test -f "$cmake_test_generated_dir/helper.cpp"
test -f "$cmake_test_generated_dir/test_harness.cpp"
(
    cd "$repo_root/tests/fixtures/project_backend_cmake"
    "$repo_root/build/dudu" test --filter cmake_backend \
        >"$repo_root/build/project_backend_cmake_test_filter.out"
)
grep -q "1/1 tests passed" "$repo_root/build/project_backend_cmake_test_filter.out"
if grep -q "ok other_test" "$repo_root/build/project_backend_cmake_test_filter.out"; then
    echo "cmake backend dudu test filter ran an unfiltered test" >&2
    exit 1
fi

(
    cd "$repo_root/tests/fixtures/project_backend_cmake_namespaces"
    "$repo_root/build/dudu" run >"$repo_root/build/project_backend_cmake_namespaces_run.out" \
        2>"$repo_root/build/project_backend_cmake_namespaces_run.err"
)
grep -Eq "run .*project_backend_cmake_namespaces/cmake-backend/build/backend_cmake_namespaces" \
    "$repo_root/build/project_backend_cmake_namespaces_run.err"
test -f "$repo_root/build/project_backend_cmake_namespaces/cmake-backend/build/generated/left.cpp"
test -f "$repo_root/build/project_backend_cmake_namespaces/cmake-backend/build/generated/right.cpp"
grep -q "DuduLeftBox" \
    "$repo_root/build/project_backend_cmake_namespaces/cmake-backend/build/generated/left.hpp"
grep -q "DuduRightBox" \
    "$repo_root/build/project_backend_cmake_namespaces/cmake-backend/build/generated/right.hpp"

(
    cd "$repo_root/tests/fixtures/project_import_metadata"
    "$repo_root/build/dudu" run >"$repo_root/build/project_import_metadata_run.out" \
        2>"$repo_root/build/project_import_metadata_run.err"
)
grep -Eq "run .*project_import_metadata/cmake-backend/build/project_import_metadata" \
    "$repo_root/build/project_import_metadata_run.err"
grep -q "42" "$repo_root/build/project_import_metadata_run.out"
test -f "$repo_root/build/project_import_metadata/cmake-backend/build/generated/camera.cpp"
test -f "$repo_root/build/project_import_metadata/cmake-backend/build/generated/renderer.cpp"

(
    cd "$repo_root/tests/fixtures/project_backend_auto_modules"
    "$repo_root/build/dudu" run >"$repo_root/build/project_backend_auto_modules_run.out" \
        2>"$repo_root/build/project_backend_auto_modules_run.err"
    "$repo_root/build/dudu" test >"$repo_root/build/project_backend_auto_modules_test.out" \
        2>"$repo_root/build/project_backend_auto_modules_test.err"
)
grep -Eq "run .*project_backend_auto_modules/cmake-backend/build/backend_auto_modules" \
    "$repo_root/build/project_backend_auto_modules_run.err"
test -f "$repo_root/build/project_backend_auto_modules/cmake-backend/build/generated/left.cpp"
test -f "$repo_root/build/project_backend_auto_modules/cmake-backend/build/generated/right.cpp"
grep -Eq "cmake .*project_backend_auto_modules/dudu-tests/main-[0-9a-f]+-cmake/source/CMakeLists.txt" \
    "$repo_root/build/project_backend_auto_modules_test.err"
grep -q "1/1 tests passed" "$repo_root/build/project_backend_auto_modules_test.out"
auto_modules_test_binary=$(sed -n 's/^test //p' "$repo_root/build/project_backend_auto_modules_test.err" | tail -1)
auto_modules_test_generated_dir="$(dirname "$auto_modules_test_binary")/generated"
test -f "$auto_modules_test_generated_dir/main.cpp"
test -f "$auto_modules_test_generated_dir/left.cpp"
test -f "$auto_modules_test_generated_dir/right.cpp"
test -f "$auto_modules_test_generated_dir/test_harness.cpp"

(
    cd "$repo_root/tests/fixtures/project_backend_auto_modules_native"
    "$repo_root/build/dudu" run >"$repo_root/build/project_backend_auto_modules_native_run.out" \
        2>"$repo_root/build/project_backend_auto_modules_native_run.err"
    "$repo_root/build/dudu" test >"$repo_root/build/project_backend_auto_modules_native_test.out" \
        2>"$repo_root/build/project_backend_auto_modules_native_test.err"
)
grep -Eq "run .*project_backend_auto_modules_native/cmake-backend/build/backend_auto_modules_native" \
    "$repo_root/build/project_backend_auto_modules_native_run.err"
grep -Eq "cmake .*project_backend_auto_modules_native/dudu-tests/main-[0-9a-f]+-cmake/source/CMakeLists.txt" \
    "$repo_root/build/project_backend_auto_modules_native_test.err"
grep -q "1/1 tests passed" "$repo_root/build/project_backend_auto_modules_native_test.out"
native_modules_test_binary=$(sed -n 's/^test //p' \
    "$repo_root/build/project_backend_auto_modules_native_test.err" | tail -1)
native_modules_test_generated_dir="$(dirname "$native_modules_test_binary")/generated"
test -f "$native_modules_test_generated_dir/main.cpp"
test -f "$native_modules_test_generated_dir/dep.cpp"
test -f "$native_modules_test_generated_dir/test_harness.cpp"

rm -rf "$repo_root/build/project_user_cmake"
(
    cd "$repo_root/tests/fixtures/project_user_cmake"
    "$repo_root/build/dudu" build --verbose 2>"$repo_root/build/project_user_cmake_build.err"
    "$repo_root/build/dudu" run >"$repo_root/build/project_user_cmake_run.out" \
        2>"$repo_root/build/project_user_cmake_run.err"
    "$repo_root/build/dudu" test --verbose >"$repo_root/build/project_user_cmake_test.out" \
        2>"$repo_root/build/project_user_cmake_test.err"
)
grep -Eq "cmake .*project_user_cmake/CMakeLists.txt" \
    "$repo_root/build/project_user_cmake_build.err"
grep -Eq "build .*project_user_cmake/cmake-user/build" \
    "$repo_root/build/project_user_cmake_build.err"
grep -Eq "run .*project_user_cmake/cmake-user/build/user_cmake_tool" \
    "$repo_root/build/project_user_cmake_run.err"
grep -q "user cmake backend" "$repo_root/build/project_user_cmake_run.out"
grep -Eq "test .*project_user_cmake/cmake-user/build" \
    "$repo_root/build/project_user_cmake_test.err"
grep -q "100% tests passed" "$repo_root/build/project_user_cmake_test.err"
if [[ -s "$repo_root/build/project_user_cmake_test.out" ]]; then
    echo "project_user_cmake test output should stream through stderr" >&2
    exit 1
fi
rm -f "$repo_root/build/project_linker_script_bin" "$repo_root/build/project_linker_script_bin.cpp"
(
    cd "$repo_root/tests/fixtures/project_linker_script"
    "$repo_root/build/duc" build -o "$repo_root/build/project_linker_script_bin" --verbose \
        2>"$repo_root/build/project_linker_script_verbose.err"
)
grep -q -- "-Wl,-T,linker.ld" \
    "$repo_root/tests/fixtures/project_linker_script/build/cmake-backend/source/CMakeLists.txt"
grep -q '__attribute__((section(".dudu_boot")))' \
    "$repo_root/tests/fixtures/project_linker_script/build/cmake-backend/build/generated/main.cpp"
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
fake_pkg_config_include="$(cd "$repo_root/tests/fixtures/project_pkg_config/include" && pwd)"
cat >"$fake_pkg_config" <<SH
#!/usr/bin/env bash
set -euo pipefail

if [[ "\$*" == "--version" ]]; then
    printf '%s\n' '1.0.0'
    exit 0
fi
if [[ "\$*" == *"--modversion"* && "\$*" == *"fixturelib"* ]]; then
    printf '%s\n' '1.0.0'
    exit 0
fi
if [[ "\$*" == *"--exists"* && "\$*" == *"fixturelib"* ]]; then
    exit 0
fi
if [[ "\$*" != *"fixturelib"* ]]; then
    echo "unexpected pkg-config args: \$*" >&2
    exit 1
fi
if [[ "\$*" == *"--cflags"* ]]; then
    printf '%s\n' '-I$fake_pkg_config_include'
fi
SH
chmod +x "$fake_pkg_config"
rm -f "$repo_root/build/project_pkg_config_bin" "$repo_root/build/project_pkg_config_bin.cpp"
rm -rf "$repo_root/tests/fixtures/project_pkg_config/build/cmake-backend"
(
    cd "$repo_root/tests/fixtures/project_pkg_config"
    PKG_CONFIG="$fake_pkg_config" "$repo_root/build/duc" build \
        -o "$repo_root/build/project_pkg_config_bin" --verbose \
        2>"$repo_root/build/project_pkg_config_verbose.err"
)
grep -q "pkg_check_modules(DUDU_PKG REQUIRED IMPORTED_TARGET fixturelib)" \
    "$repo_root/tests/fixtures/project_pkg_config/build/cmake-backend/source/CMakeLists.txt"
grep -q "target_link_libraries(main PRIVATE PkgConfig::DUDU_PKG)" \
    "$repo_root/tests/fixtures/project_pkg_config/build/cmake-backend/source/CMakeLists.txt"
set +e
"$repo_root/build/project_pkg_config_bin"
project_pkg_config_status=$?
set -e
if [[ "$project_pkg_config_status" -ne 42 ]]; then
    echo "project_pkg_config returned $project_pkg_config_status, expected 42" >&2
    exit 1
fi
rm -f "$repo_root/build/libproject_library.a" "$repo_root/build/libproject_library.a.cpp"
(
    cd "$repo_root/tests/fixtures/project_library"
    "$repo_root/build/duc" build -o "$repo_root/build/libproject_library.a" --verbose \
        2>"$repo_root/build/project_library_verbose.err"
)
test -f "$repo_root/build/libproject_library.a"
ar t "$repo_root/build/libproject_library.a" | grep -q "\.cpp.o"
grep -q "add_library(project_library STATIC" \
    "$repo_root/tests/fixtures/project_library/build/cmake-backend/source/CMakeLists.txt"
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
rm -f "$repo_root/build/libproject_shared.so" "$repo_root/build/libproject_shared.so.cpp"
(
    cd "$repo_root/tests/fixtures/project_shared_library"
    "$repo_root/build/duc" build -o "$repo_root/build/libproject_shared.so" --verbose \
        2>"$repo_root/build/project_shared_library_verbose.err"
    "$repo_root/build/duc" main.dd --emit-header "$repo_root/build/project_shared_library.hpp"
)
test -f "$repo_root/build/libproject_shared.so"
grep -q "add_library(project_shared SHARED" \
    "$repo_root/tests/fixtures/project_shared_library/build/cmake-backend/source/CMakeLists.txt"
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
rm -f "$repo_root/build/libproject_freestanding.a" "$repo_root/build/libproject_freestanding.a.cpp"
(
    cd "$repo_root/tests/fixtures/project_freestanding_mode"
    "$repo_root/build/duc" build -o "$repo_root/build/libproject_freestanding.a" --verbose \
        2>"$repo_root/build/project_freestanding_verbose.err"
)
test -f "$repo_root/build/libproject_freestanding.a"
freestanding_cmake="$repo_root/tests/fixtures/project_freestanding_mode/build/cmake-backend/source/CMakeLists.txt"
freestanding_cpp="$repo_root/tests/fixtures/project_freestanding_mode/build/cmake-backend/build/generated/main.cpp"
grep -q -- "-ffreestanding" "$freestanding_cmake"
grep -q -- "-fno-exceptions" "$freestanding_cmake"
grep -q -- "-fno-rtti" "$freestanding_cmake"
if grep -q "#include <iostream>" "$freestanding_cpp"; then
    echo "freestanding prelude unexpectedly included iostream" >&2
    exit 1
fi
rm -f "$repo_root/build/libproject_embedded_uart.a" "$repo_root/build/libproject_embedded_uart.a.cpp"
(
    cd "$repo_root/tests/fixtures/project_embedded_uart"
    "$repo_root/build/duc" build -o "$repo_root/build/libproject_embedded_uart.a" --verbose \
        2>"$repo_root/build/project_embedded_uart_verbose.err"
)
test -f "$repo_root/build/libproject_embedded_uart.a"
embedded_uart_cmake="$repo_root/tests/fixtures/project_embedded_uart/build/cmake-backend/source/CMakeLists.txt"
embedded_uart_cpp="$repo_root/tests/fixtures/project_embedded_uart/build/cmake-backend/build/generated/main.cpp"
grep -q -- "-ffreestanding" "$embedded_uart_cmake"
grep -q -- "-fno-exceptions" "$embedded_uart_cmake"
grep -q -- "-fno-rtti" "$embedded_uart_cmake"
grep -q "volatile uint32_t status" "$embedded_uart_cpp"
grep -q "reinterpret_cast<volatile DuduMainUartRegs\\*>" "$embedded_uart_cpp"
if (
    cd "$repo_root/tests/fixtures/project_library"
    "$repo_root/build/duc" run -o "$repo_root/build/project_library_run"
) 2>"$repo_root/build/project_library_run.err"; then
    echo "project_library run unexpectedly passed" >&2
    exit 1
fi
grep -q "cannot run target kind: library" "$repo_root/build/project_library_run.err"

